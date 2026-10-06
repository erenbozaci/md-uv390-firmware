import csv
import os
import sys
import time

from PyQt5.QtCore import QSettings, QStandardPaths, Qt, QThread, QTimer, pyqtSignal
from PyQt5.QtGui import QBrush, QColor, QKeySequence
from PyQt5.QtWidgets import (QAbstractItemView, QApplication, QCheckBox, QComboBox, QDialog, QDialogButtonBox,
                             QDoubleSpinBox, QFileDialog, QFormLayout, QFrame, QGroupBox, QHBoxLayout, QLabel,
                             QLineEdit, QListWidget, QListWidgetItem, QMainWindow, QMenu, QMessageBox, QProgressBar,
                             QPushButton, QScrollArea, QShortcut, QSizePolicy, QSpinBox, QSplitter, QStackedWidget,
                             QTableWidget, QTableWidgetItem, QVBoxLayout, QWidget)

from . import __version__, codeplug
from .codeplug import CALL_TYPES, CTCSS, Codeplug
from .protocol import Radio, RadioError, find_ports

STYLE = """
QListWidget#nav { border: none; background: palette(window); font-size: 14px; outline: 0; }
QListWidget#nav::item { padding: 11px 18px; border-radius: 7px; margin: 2px 8px; }
QListWidget#nav::item:selected { background: palette(highlight); color: palette(highlighted-text); }
QPushButton#primary { background: #1f6feb; color: white; border: none; border-radius: 6px; padding: 7px 16px;
                      font-weight: 600; }
QPushButton#primary:hover { background: #3b82f6; }
QPushButton#primary:disabled { background: #8a8f98; color: #e6e6e6; }
QPushButton#danger { color: #d1242f; }
QLabel#title { font-size: 18px; font-weight: 600; }
QLabel#muted { color: palette(mid); }
QGroupBox { font-weight: 600; margin-top: 18px; padding: 16px 10px 10px 10px; border: 1px solid palette(mid);
            border-radius: 8px; }
QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 4px; }
QTableWidget { gridline-color: transparent; }
"""

NEW_BRUSH = QBrush(QColor(46, 160, 67, 70))
MOD_BRUSH = QBrush(QColor(210, 153, 34, 80))

DCS_CODES = ("023 025 026 031 032 036 043 047 051 053 054 065 071 072 073 074 114 115 116 122 125 131 132 134 143 "
             "145 152 155 156 162 165 172 174 205 212 223 225 226 243 244 245 246 251 252 255 261 263 265 266 271 "
             "274 306 311 315 325 331 332 343 346 351 356 364 365 371 411 412 413 423 431 432 445 446 452 454 455 "
             "462 464 465 466 503 506 516 523 526 532 546 565 606 612 624 627 631 632 654 662 664 703 712 723 731 "
             "732 734 743 754").split()
TONES = ["None"] + CTCSS + ["D%sN" % c for c in DCS_CODES] + ["D%sI" % c for c in DCS_CODES]


def documents_dir():
    d = QStandardPaths.writableLocation(QStandardPaths.DocumentsLocation) or os.path.expanduser("~")
    d = os.path.join(d, "UV390CPS backups")
    os.makedirs(d, exist_ok=True)
    return d


# ------------------------------------------------------------------ worker threads
class ReadThread(QThread):
    progress = pyqtSignal(int, int)
    done = pyqtSignal(object, object)
    failed = pyqtSignal(str)

    def __init__(self, port):
        super().__init__()
        self.port = port

    def run(self):
        try:
            radio = Radio(self.port)
            try:
                info = radio.info()
                img = codeplug.read_image(radio, lambda n, t: self.progress.emit(n, t))
            finally:
                radio.close()
            self.done.emit(img, info)
        except (RadioError, KeyError, OSError) as e:
            self.failed.emit(str(e))


class WriteThread(QThread):
    progress = pyqtSignal(int, int, str)
    done = pyqtSignal(str)
    failed = pyqtSignal(str)

    def __init__(self, port, new_img, spans, backup_dir):
        super().__init__()
        self.port, self.new_img, self.spans, self.backup_dir = port, new_img, spans, backup_dir

    def run(self):
        backup = None
        radio = None
        started = False
        try:
            radio = Radio(self.port)
            info = radio.info()
            if info["type"] != "MD-UV380 / UV390":
                raise RadioError("This radio is a %s. Writing is only enabled for the MD-UV380 / UV390." % info["type"])
            self.progress.emit(0, 1, "Saving a backup of the radio")
            current = codeplug.read_image(radio, lambda n, t: self.progress.emit(n, t, "Saving a backup of the radio"))
            backup = os.path.join(self.backup_dir, time.strftime("uv390-before-write-%Y%m%d-%H%M%S.json"))
            current.save(backup)
            radio.begin_write()
            started = True
            radio.write_spans(self.spans, lambda n, t: self.progress.emit(n, t, "Writing"))
            for k, (addr, data) in enumerate(self.spans):
                self.progress.emit(k, len(self.spans), "Verifying")
                if radio.read(1, addr, len(data)) != data:
                    raise RadioError("Verification failed at 0x%X" % addr)
            radio.finish_and_reboot()
            self.done.emit(backup)
        except (RadioError, KeyError, OSError) as e:
            if radio and started:
                try:
                    radio.reboot()
                except Exception:
                    pass
            self.failed.emit("%s%s" % (e, ("\n\nBackup of the radio before writing:\n%s" % backup) if backup else ""))
        finally:
            if radio:
                radio.close()


# ------------------------------------------------------------------ form widgets
class Members(QWidget):
    """Ordered list of numbers (channels in a zone, contacts in a TG list) with add / remove / move."""
    changed = pyqtSignal()

    def __init__(self, name_of, universe, limit, noun, open_cb=None):
        super().__init__()
        self.open_cb = open_cb
        self.name_of, self.universe, self.limit_of, self.noun = name_of, universe, limit, noun
        self.values = []
        self.list = QListWidget()
        self.list.setSelectionMode(QAbstractItemView.ExtendedSelection)
        self.list.setContextMenuPolicy(Qt.CustomContextMenu)
        self.list.customContextMenuRequested.connect(self.menu)
        self.list.itemDoubleClicked.connect(lambda it: self.open(it.data(Qt.UserRole)))
        self.count = QLabel()
        self.count.setObjectName("muted")
        add = QPushButton("Add…")
        rem = QPushButton("Remove")
        up = QPushButton("Up")
        down = QPushButton("Down")
        add.clicked.connect(self.add)
        rem.clicked.connect(self.remove)
        up.clicked.connect(lambda: self.move(-1))
        down.clicked.connect(lambda: self.move(1))
        row = QHBoxLayout()
        for b in (add, rem, up, down):
            row.addWidget(b)
        row.addStretch()
        row.addWidget(self.count)
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.addWidget(self.list)
        lay.addLayout(row)

    def set(self, values):
        self.values = list(values)
        self.redraw()

    def redraw(self, select=()):
        self.list.clear()
        for v in self.values:
            it = QListWidgetItem("%d   %s" % (v, self.name_of(v)))
            it.setData(Qt.UserRole, v)
            self.list.addItem(it)
            if v in select:
                it.setSelected(True)
        self.count.setText("%d / %d" % (len(self.values), self.limit_of()))

    def open(self, n):
        if self.open_cb and n is not None:
            self.open_cb(n)

    def menu(self, pos):
        item = self.list.itemAt(pos)
        if item is None:
            return
        if not item.isSelected():
            self.list.clearSelection()
            item.setSelected(True)
        many = len(self.list.selectedItems()) > 1
        m = QMenu(self)
        edit = m.addAction("Edit %s…" % self.noun)
        edit.setEnabled(bool(self.open_cb) and not many)
        m.addSeparator()
        up = m.addAction("Move up")
        down = m.addAction("Move down")
        m.addSeparator()
        rem = m.addAction("Remove from list")
        act = m.exec_(self.list.viewport().mapToGlobal(pos))
        if act is edit:
            self.open(item.data(Qt.UserRole))
        elif act is up:
            self.move(-1)
        elif act is down:
            self.move(1)
        elif act is rem:
            self.remove()

    def add(self):
        free = [(n, nm) for n, nm in self.universe() if n not in self.values]
        dlg = PickDialog(self, "Add %s" % self.noun, free)
        if dlg.exec_():
            picked = dlg.picked()
            room = self.limit_of() - len(self.values)
            if len(picked) > room:
                QMessageBox.warning(self, "Limit", "Only %d more fit; the rest were skipped." % max(room, 0))
                picked = picked[:max(room, 0)]
            self.values += picked
            self.redraw(picked)
            self.changed.emit()

    def remove(self):
        rm = {i.data(Qt.UserRole) for i in self.list.selectedItems()}
        if rm:
            self.values = [v for v in self.values if v not in rm]
            self.redraw()
            self.changed.emit()

    def move(self, d):
        rows = sorted(self.list.row(i) for i in self.list.selectedItems())
        if not rows or (d < 0 and rows[0] == 0) or (d > 0 and rows[-1] == len(self.values) - 1):
            return
        for r in (rows if d < 0 else reversed(rows)):
            self.values[r + d], self.values[r] = self.values[r], self.values[r + d]
        self.redraw({self.values[r + d] for r in rows})
        self.changed.emit()


class PickDialog(QDialog):
    def __init__(self, parent, title, items):
        super().__init__(parent)
        self.setWindowTitle(title)
        self.resize(380, 480)
        self.search = QLineEdit()
        self.search.setPlaceholderText("Search")
        self.search.setClearButtonEnabled(True)
        self.list = QListWidget()
        self.list.setSelectionMode(QAbstractItemView.ExtendedSelection)
        for n, name in items:
            it = QListWidgetItem("%d   %s" % (n, name))
            it.setData(Qt.UserRole, n)
            self.list.addItem(it)
        self.search.textChanged.connect(self.filter)
        self.list.itemDoubleClicked.connect(lambda *_: self.accept())
        bb = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        bb.accepted.connect(self.accept)
        bb.rejected.connect(self.reject)
        lay = QVBoxLayout(self)
        for w in (self.search, self.list, bb):
            lay.addWidget(w)

    def filter(self, text):
        t = text.lower()
        for i in range(self.list.count()):
            self.list.item(i).setHidden(t not in self.list.item(i).text().lower())

    def picked(self):
        return [i.data(Qt.UserRole) for i in sorted(self.list.selectedItems(), key=self.list.row)]


class Form(QWidget):
    """Declarative form bound to one record dict. field = dict(key, label, kind, ...)."""
    edited = pyqtSignal(str)

    def __init__(self, fields):
        super().__init__()
        self.fields = fields
        self.rec = None         # first selected record
        self.recs = []          # all selected records, edits go to every one of them
        self.w = {}
        self.layout_ = QFormLayout(self)
        self.layout_.setLabelAlignment(Qt.AlignRight | Qt.AlignTop)
        self.layout_.setFieldGrowthPolicy(QFormLayout.AllNonFixedFieldsGrow)
        for f in fields:
            self._make(f)

    def _make(self, f):
        k, key = f["kind"], f["key"]
        if k == "text":
            w = QLineEdit()
            w.setMaxLength(f.get("max", 16))
            w.textEdited.connect(lambda *_, key=key: self._edited(key))
            get, put = (lambda w=w: w.text()), (lambda v, w=w: w.setText(v))
        elif k == "freq":
            w = QDoubleSpinBox()
            w.setDecimals(5)
            w.setRange(0, 999.99999)
            w.setSingleStep(0.0125)
            w.setSuffix(" MHz")
            w.setKeyboardTracking(False)
            w.valueChanged.connect(lambda *_, key=key: self._edited(key))
            get, put = (lambda w=w: round(w.value(), 5)), (lambda v, w=w: w.setValue(v))
        elif k == "spin":
            w = QSpinBox()
            w.setRange(*f["range"])
            w.valueChanged.connect(lambda *_, key=key: self._edited(key))
            get, put = (lambda w=w: w.value()), (lambda v, w=w: w.setValue(v))
        elif k == "check":
            w = QCheckBox(f.get("text", ""))
            w.toggled.connect(lambda *_, key=key: self._edited(key))
            get, put = (lambda w=w: w.isChecked()), (lambda v, w=w: w.setChecked(bool(v)))
        elif k == "combo":
            w = QComboBox()
            for data, label in f["items"]:
                w.addItem(label, data)
            w.currentIndexChanged.connect(lambda *_, key=key: self._edited(key))
            get = lambda w=w: w.currentData()
            put = lambda v, w=w: w.setCurrentIndex(max(w.findData(v), 0))
        elif k == "ref":   # combo filled from the codeplug each time the form is bound
            w = QComboBox()
            w.currentIndexChanged.connect(lambda *_, key=key: self._edited(key))
            get = lambda w=w: w.currentData()
            put = lambda v, w=w: w.setCurrentIndex(max(w.findData(v), 0))
        elif k == "tone":
            w = QComboBox()
            w.setEditable(True)
            w.addItems(TONES)
            w.setInsertPolicy(QComboBox.NoInsert)
            w.currentTextChanged.connect(lambda *_, key=key: self._edited(key))

            def get(w=w):
                t = w.currentText().strip().upper()
                if t in ("", "NONE"):
                    return ""
                try:
                    codeplug.tone_from_str(t)
                    if t[0] == "D" and len(t) != 5:
                        raise ValueError
                except (ValueError, IndexError):
                    return None
                return t

            put = lambda v, w=w: w.setCurrentText(v or "None")
        elif k == "members":
            w = f["make"]()
            w.changed.connect(lambda key=key: self._edited(key))
            get, put = (lambda w=w: list(w.values)), (lambda v, w=w: w.set(v))
        else:
            raise ValueError(k)
        self.w[key] = (w, get, put, f)
        self.layout_.addRow(f["label"], w)

    def bind(self, recs):
        if recs is not None and not isinstance(recs, list):
            recs = [recs]
        rec = recs[0] if recs else None
        self.rec, self.recs = None, []
        for key, (w, get, put, f) in self.w.items():
            if f["kind"] == "ref":
                w.blockSignals(True)
                w.clear()
                for data, label in f["items"]():
                    w.addItem(label, data)
                w.blockSignals(False)
        for key, (w, get, put, f) in self.w.items():
            w.blockSignals(True)
            if rec is not None:
                put(rec[key])
            w.blockSignals(False)
        self.rec, self.recs = rec, recs or []
        self._visibility()

    def _visibility(self):
        for key, (w, get, put, f) in self.w.items():
            show = f.get("show")
            vis = self.rec is not None and (show is None or show(self.rec)) and not (f.get("single") and len(self.recs) > 1)
            w.setVisible(vis)
            lab = self.layout_.labelForField(w)
            if lab:
                lab.setVisible(vis)

    def _edited(self, key):
        if self.rec is None:
            return
        w, get, put, f = self.w[key]
        v = get()
        if v is None:       # invalid input (bad tone)
            w.setStyleSheet("color: #d1242f")
            return
        w.setStyleSheet("")
        show = f.get("show")
        for r in self.recs:
            if show is None or show(r):
                r[key] = list(v) if isinstance(v, list) else v
        self._visibility()
        self.edited.emit(key)


# ------------------------------------------------------------------ entity pages
class Page(QWidget):
    """Table on the left, form on the right, search + add / duplicate / delete on top."""
    noun = "item"
    columns = []
    fields = []
    min_form = 440

    def __init__(self, win):
        super().__init__()
        self.win = win
        self.search = QLineEdit()
        self.search.setPlaceholderText("Search %ss" % self.noun)
        self.search.setClearButtonEnabled(True)
        self.search.textChanged.connect(self.apply_filter)
        self.b_add = QPushButton("+ New %s" % self.noun)
        self.b_dup = QPushButton("Duplicate")
        self.b_del = QPushButton("Delete")
        self.b_del.setObjectName("danger")
        self.b_add.clicked.connect(self.add)
        self.b_dup.clicked.connect(self.duplicate)
        self.b_del.clicked.connect(self.delete)
        bar = QHBoxLayout()
        bar.addWidget(self.search, 1)
        for b in (self.b_add, self.b_dup, self.b_del):
            bar.addWidget(b)

        self.table = QTableWidget(0, len(self.columns))
        self.table.setHorizontalHeaderLabels(self.columns)
        self.table.setEditTriggers(QAbstractItemView.NoEditTriggers)
        self.table.setSelectionBehavior(QAbstractItemView.SelectRows)
        self.table.setSelectionMode(QAbstractItemView.ExtendedSelection)
        self.table.setAlternatingRowColors(True)
        self.table.setSortingEnabled(True)
        self.table.sortByColumn(0, Qt.AscendingOrder)
        self.table.verticalHeader().setVisible(False)
        self.table.horizontalHeader().setStretchLastSection(True)
        self.table.itemSelectionChanged.connect(self.selected)

        self.form = Form(self.fields)
        self.form.edited.connect(self.on_edited)
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.NoFrame)
        scroll.setWidget(self.form)
        scroll.setMinimumWidth(self.min_form)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        self.hint = QLabel("Select a %s on the left to edit it." % self.noun)
        self.hint.setObjectName("muted")
        self.hint.setAlignment(Qt.AlignCenter)
        right = QWidget()
        rl = QVBoxLayout(right)
        rl.setContentsMargins(0, 0, 0, 0)
        rl.addWidget(self.hint)
        rl.addWidget(scroll)
        self.scroll = scroll
        self.banner = QLabel()
        self.banner.setWordWrap(True)
        self.banner.setStyleSheet("background: rgba(31,111,235,40); border-radius: 6px; padding: 8px;")
        self.banner.setVisible(False)
        rl.insertWidget(0, self.banner)
        scroll.setVisible(False)

        split = QSplitter()
        split.addWidget(self.table)
        split.addWidget(right)
        split.setStretchFactor(0, 3)
        split.setStretchFactor(1, 2)
        lay = QVBoxLayout(self)
        lay.addLayout(bar)
        lay.addWidget(split, 1)
        self.status = QLabel()
        self.status.setObjectName("muted")
        lay.addWidget(self.status)
        sc = QShortcut(QKeySequence.Delete, self.table, activated=self.delete)
        sc.setContext(Qt.WidgetShortcut)

    # to override
    def records(self):
        raise NotImplementedError

    def key_of(self, rec):
        return rec["number"]

    def row_of(self, rec):
        raise NotImplementedError

    def create(self):
        raise NotImplementedError

    def remove(self, rec):
        raise NotImplementedError

    def copy_into(self, new, old):
        for k, v in old.items():
            if k not in ("number", "slot", "raw", "_orig"):
                new[k] = list(v) if isinstance(v, list) else v
        new["name"] = (old["name"] + " copy")[:16]

    def find(self, key):
        for r in self.records():
            if self.key_of(r) == key:
                return r

    # shared logic
    def refresh(self):
        keep = self.current_key()
        self.table.setSortingEnabled(False)
        recs = self.records()
        self.table.setRowCount(len(recs))
        for r, rec in enumerate(recs):
            self.fill_row(r, rec)
        self.table.setSortingEnabled(True)
        self.table.resizeColumnsToContents()
        self.apply_filter()
        if keep is not None:
            self.select_key(keep)
        self.update_status()

    def fill_row(self, r, rec):
        brush = {"new": NEW_BRUSH, "mod": MOD_BRUSH}.get(Codeplug.state(rec))
        for c, v in enumerate(self.row_of(rec)):
            it = self.table.item(r, c)
            fresh = it is None
            if fresh:
                it = QTableWidgetItem()
            it.setData(Qt.DisplayRole, v)
            if c == 0:
                it.setData(Qt.UserRole, self.key_of(rec))
            it.setBackground(brush or QBrush())
            if fresh:
                self.table.setItem(r, c, it)

    def row_index(self, key):
        for r in range(self.table.rowCount()):
            if self.table.item(r, 0).data(Qt.UserRole) == key:
                return r

    def current_key(self):
        rows = self.table.selectionModel().selectedRows()
        return self.table.item(rows[0].row(), 0).data(Qt.UserRole) if rows else None

    def select_key(self, key):
        r = self.row_index(key)
        if r is not None:
            self.table.selectRow(r)
            self.table.scrollToItem(self.table.item(r, 0))

    def apply_filter(self):
        t = self.search.text().lower()
        for r in range(self.table.rowCount()):
            hide = bool(t) and not any(t in (self.table.item(r, c).text().lower()) for c in range(self.table.columnCount()))
            self.table.setRowHidden(r, hide)

    def selected_keys(self):
        return [self.table.item(i.row(), 0).data(Qt.UserRole) for i in self.table.selectionModel().selectedRows()]

    def selected(self):
        recs = [r for r in (self.find(k) for k in self.selected_keys()) if r is not None]
        many = len(recs) > 1
        self.hint.setVisible(not recs)
        self.scroll.setVisible(bool(recs))
        self.banner.setVisible(many)
        if many:
            self.banner.setText("%d %ss selected. Only the fields you change are applied to all of them." % (len(recs), self.noun))
        self.b_dup.setEnabled(len(recs) == 1)
        self.b_del.setEnabled(bool(recs))
        self.form.bind(recs or None)

    def on_edited(self, key):
        self.table.setSortingEnabled(False)
        for rec in self.form.recs:
            r = self.row_index(self.key_of(rec))
            if r is not None:
                self.fill_row(r, rec)
        self.table.setSortingEnabled(True)
        self.win.changed()

    def add(self):
        key = self.create()
        if key is None:
            QMessageBox.information(self, "Full", "No free %s slot left." % self.noun)
            return
        self.search.clear()
        self.refresh()
        self.select_key(key)
        self.win.changed()

    def duplicate(self):
        rec = self.find(self.current_key())
        if rec is None or len(self.selected_keys()) != 1:
            return
        key = self.create()
        if key is None:
            QMessageBox.information(self, "Full", "No free %s slot left." % self.noun)
            return
        self.copy_into(self.find(key), rec)
        self.refresh()
        self.select_key(key)
        self.win.changed()

    def delete(self):
        keys = [self.table.item(i.row(), 0).data(Qt.UserRole) for i in self.table.selectionModel().selectedRows()]
        if not keys:
            return
        note = self.delete_note(keys)
        if QMessageBox.question(self, "Delete", "Delete %d %s(s)?%s" % (len(keys), self.noun, note)) != QMessageBox.Yes:
            return
        for k in keys:
            self.remove(self.find(k))
        self.refresh()
        self.selected()
        self.win.changed()

    def delete_note(self, keys):
        return ""

    def update_status(self):
        self.status.setText("%d %ss" % (len(self.records()), self.noun))


class ChannelPage(Page):
    noun = "channel"
    columns = ["No", "Name", "Mode", "RX MHz", "TX MHz", "Power", "BW", "RX tone", "TX tone", "CC", "TS", "Contact"]

    def __init__(self, win):
        cp = win.cp_getter
        digital = lambda r: r["mode"] == "DMR"
        analog = lambda r: r["mode"] == "FM"
        self.fields = [
            dict(key="name", label="Name", kind="text", single=True),
            dict(key="mode", label="Mode", kind="combo", items=[("FM", "Analog FM"), ("DMR", "Digital DMR")]),
            dict(key="rx", label="RX frequency", kind="freq"),
            dict(key="tx", label="TX frequency", kind="freq"),
            dict(key="power", label="Power", kind="combo", items=list(enumerate(codeplug.POWER_LABELS))),
            dict(key="bw", label="Bandwidth", kind="combo", show=analog, items=[("12.5", "12.5 kHz"), ("25", "25 kHz")]),
            dict(key="rxtone", label="RX tone", kind="tone", show=analog),
            dict(key="txtone", label="TX tone", kind="tone", show=analog),
            dict(key="colour", label="Colour code", kind="spin", range=(0, 15), show=digital),
            dict(key="slot", label="Time slot", kind="combo", show=digital, items=[(1, "Slot 1"), (2, "Slot 2")]),
            dict(key="contact", label="Contact", kind="ref", show=digital,
                 items=lambda: [(0, "None")] + [(n, "%s (%d)" % (c["name"], c["id"])) for n, c in sorted(cp().contacts.items())]),
            dict(key="tglist", label="TG list", kind="ref", show=digital,
                 items=lambda: [(0, "None")] + [(n, t["name"]) for n, t in sorted(cp().tglists.items())]),
            dict(key="rxonly", label="Receive only", kind="check"),
            dict(key="zoneskip", label="Skip in zone scan", kind="check"),
            dict(key="allskip", label="Skip in all-channel scan", kind="check"),
        ]
        super().__init__(win)

    def records(self):
        return [self.win.cp.channels[n] for n in sorted(self.win.cp.channels)]

    def row_of(self, c):
        digital = c["mode"] == "DMR"
        contact = self.win.cp.contacts.get(c["contact"])
        return (c["number"], c["name"], c["mode"], c["rx"], c["tx"], codeplug.POWER_LABELS[c["power"]]
                if c["power"] < len(codeplug.POWER_LABELS) else str(c["power"]), "" if digital else c["bw"],
                "" if digital else c["rxtone"], "" if digital else c["txtone"],
                c["colour"] if digital else "", c["slot"] if digital else "",
                (contact["name"] if contact else "") if digital else "")

    def create(self):
        return self.win.cp.new_channel()

    def remove(self, rec):
        self.win.cp.delete_channel(rec["number"])

    def selected(self):
        super().selected()
        rec = self.form.rec
        self.simplex = rec is not None and len(self.form.recs) == 1 and rec["rx"] == rec["tx"]

    def on_edited(self, key):
        rec = self.form.rec
        if key == "rx" and self.simplex:        # a simplex channel stays simplex while RX is typed
            rec["tx"] = rec["rx"]
            w = self.form.w["tx"][0]
            w.blockSignals(True)
            w.setValue(rec["tx"])
            w.blockSignals(False)
        elif key == "tx":
            self.simplex = rec["rx"] == rec["tx"]
        super().on_edited(key)

    def delete_note(self, keys):
        zones = sum(1 for z in self.win.cp.zones if any(k in z["channels"] for k in keys))
        return "\nThey will also be removed from %d zone(s)." % zones if zones else ""


class ContactPage(Page):
    noun = "contact"
    columns = ["No", "Name", "DMR ID", "Type"]
    fields = [
        dict(key="name", label="Name", kind="text", single=True),
        dict(key="id", label="DMR ID / Talkgroup", kind="spin", range=(1, 16777215)),
        dict(key="type", label="Call type", kind="combo", items=[(t, t) for t in CALL_TYPES]),
    ]

    def records(self):
        return [self.win.cp.contacts[n] for n in sorted(self.win.cp.contacts)]

    def row_of(self, c):
        return (c["number"], c["name"], c["id"], c["type"])

    def create(self):
        return self.win.cp.new_contact()

    def remove(self, rec):
        self.win.cp.delete_contact(rec["number"])

    def delete_note(self, keys):
        n = sum(1 for c in self.win.cp.channels.values() if c["contact"] in keys)
        return "\n%d channel(s) use it; they will be set to no contact." % n if n else ""


class TGListPage(Page):
    noun = "TG list"
    columns = ["No", "Name", "Contacts"]

    def __init__(self, win):
        cp = win.cp_getter
        self.fields = [
            dict(key="name", label="Name", kind="text", single=True),
            dict(key="contacts", label="Contacts", kind="members", single=True, make=lambda: Members(
                lambda n: cp().contacts[n]["name"] if n in cp().contacts else "(missing)",
                lambda: [(n, c["name"]) for n, c in sorted(cp().contacts.items())], lambda: 32, "contact",
                lambda n: win.goto(2, n))),
        ]
        super().__init__(win)

    def records(self):
        return [self.win.cp.tglists[n] for n in sorted(self.win.cp.tglists)]

    def row_of(self, t):
        names = self.win.cp.contacts
        return (t["number"], t["name"], ", ".join(names[c]["name"] if c in names else str(c) for c in t["contacts"]))

    def create(self):
        return self.win.cp.new_tglist()

    def remove(self, rec):
        self.win.cp.delete_tglist(rec["number"])

    def selected(self):
        super().selected()
        rec = self.form.rec
        if rec is not None and not rec["contacts"]:
            self.status.setText("A list without contacts is ignored by the radio.")
        else:
            self.update_status()


class ZonePage(Page):
    noun = "zone"
    columns = ["No", "Name", "Channels"]

    def __init__(self, win):
        cp = win.cp_getter
        self.fields = [
            dict(key="name", label="Name", kind="text", single=True),
            dict(key="channels", label="Channels", kind="members", single=True, make=lambda: Members(
                lambda n: cp().channels[n]["name"] if n in cp().channels else "(missing)",
                lambda: [(n, c["name"]) for n, c in sorted(cp().channels.items())], lambda: cp().per_zone, "channel",
                lambda n: win.goto(1, n))),
        ]
        super().__init__(win)

    def records(self):
        return self.win.cp.zones

    def key_of(self, rec):
        return rec["slot"]

    def row_of(self, z):
        return (self.win.cp.zones.index(z) + 1, z["name"], len(z["channels"]))

    def create(self):
        return self.win.cp.new_zone()

    def remove(self, rec):
        self.win.cp.delete_zone(rec["slot"])


class RadioPage(QWidget):
    def __init__(self, win):
        super().__init__()
        self.win = win
        self.port_box = QComboBox()
        self.port_box.setMinimumWidth(300)
        refresh = QPushButton("Refresh")
        refresh.clicked.connect(self.refresh_ports)
        self.read_btn = QPushButton("Read from radio")
        self.read_btn.setObjectName("primary")
        self.read_btn.clicked.connect(win.read_radio)
        row = QHBoxLayout()
        row.addWidget(self.port_box, 1)
        row.addWidget(refresh)
        row.addWidget(self.read_btn)
        self.conn_note = QLabel("Turn the radio on (normal mode, not DFU or hotspot), connect USB, press Read.")
        self.conn_note.setObjectName("muted")
        self.conn_note.setWordWrap(True)
        g1 = QGroupBox("Connection")
        l1 = QVBoxLayout(g1)
        l1.addLayout(row)
        l1.addWidget(self.conn_note)

        self.info = QLabel("Nothing loaded yet.")
        self.info.setWordWrap(True)
        g2 = QGroupBox("Codeplug")
        QVBoxLayout(g2).addWidget(self.info)

        self.ident = Form([dict(key="callsign", label="Callsign", kind="text", max=8),
                           dict(key="dmrid", label="DMR ID", kind="spin", range=(0, 16777215))])
        self.ident.edited.connect(lambda k: win.changed())
        g3 = QGroupBox("Identity")
        QVBoxLayout(g3).addWidget(self.ident)

        b_save = QPushButton("Save backup…")
        b_open = QPushButton("Open backup…")
        b_save.clicked.connect(win.save_image)
        b_open.clicked.connect(win.open_image)
        g4 = QGroupBox("Backup file")
        l4 = QHBoxLayout(g4)
        l4.addWidget(b_save)
        l4.addWidget(b_open)
        l4.addStretch()

        lay = QVBoxLayout(self)
        for g in (g1, g2, g3, g4):
            lay.addWidget(g)
        lay.addStretch()
        self.refresh_ports()

    def refresh_ports(self):
        self.port_box.clear()
        for p in find_ports():
            self.port_box.addItem("%s   %s" % (p.device, p.description or ""), p.device)

    def refresh(self):
        cp = self.win.cp
        if cp is None:
            self.ident.bind(None)
            return
        self.ident.bind(cp.general)
        lines = []
        if self.win.radio_info:
            i = self.win.radio_info
            lines.append("Radio: %s, firmware %s, built %s" % (i["type"], i["git"].strip('"'), i["built"]))
        else:
            lines.append("Loaded from a backup file.")
        lines.append("%d channels, %d contacts, %d TG lists, %d zones (%d channels per zone)" % (
            len(cp.channels), len(cp.contacts), len(cp.tglists), len(cp.zones), cp.per_zone))
        self.info.setText("\n".join(lines))


# ------------------------------------------------------------------ main window
class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("UV390 CPS")
        self.resize(1200, 700)
        self.cp = None
        self.radio_info = None
        self.thread = None
        self.finishing = []
        self.cp_getter = lambda: self.cp

        self.nav = QListWidget()
        self.nav.setObjectName("nav")
        self.nav.setFixedWidth(180)
        self.stack = QStackedWidget()
        self.pages = [RadioPage(self), ChannelPage(self), ContactPage(self), TGListPage(self), ZonePage(self)]
        for name, page in zip(("Radio", "Channels", "Contacts", "TG lists", "Zones"), self.pages):
            self.nav.addItem(name)
            self.stack.addWidget(page)
        self.nav.currentRowChanged.connect(self.show_page)

        title = QLabel("UV390 CPS")
        title.setObjectName("title")
        self.dirty = QLabel()
        self.dirty.setObjectName("muted")
        self.b_revert = QPushButton("Revert")
        self.b_revert.clicked.connect(self.revert)
        self.b_write = QPushButton("Write to radio")
        self.b_write.setObjectName("primary")
        self.b_write.clicked.connect(self.write_radio)
        self.progress = QProgressBar()
        self.progress.setFixedWidth(220)
        self.progress.setVisible(False)
        self.progress_text = QLabel()
        head = QHBoxLayout()
        head.setContentsMargins(12, 8, 12, 4)
        for w in (title, self.dirty, self.progress_text, self.progress):
            head.addWidget(w)
        head.insertStretch(2, 1)
        head.addWidget(self.b_revert)
        head.addWidget(self.b_write)

        body = QHBoxLayout()
        body.addWidget(self.nav)
        body.addWidget(self.stack, 1)
        root = QWidget()
        lay = QVBoxLayout(root)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.addLayout(head)
        lay.addLayout(body, 1)
        self.setCentralWidget(root)

        m = self.menuBar().addMenu("&File")
        for text, fn, key in (("Read from radio", self.read_radio, "Ctrl+R"), ("Write to radio", self.write_radio, "Ctrl+W"),
                              ("Open backup…", self.open_image, "Ctrl+O"), ("Save backup…", self.save_image, "Ctrl+S"),
                              ("Export channels CSV…", lambda: self.export_csv(1), None),
                              ("Export contacts CSV…", lambda: self.export_csv(2), None), ("Quit", self.close, "Ctrl+Q")):
            a = m.addAction(text)
            a.triggered.connect(fn)
            if key:
                a.setShortcut(key)
        self.timer = QTimer(self)
        self.timer.setSingleShot(True)
        self.timer.timeout.connect(self.update_dirty)
        self.settings = QSettings("uv390cps", "uv390cps")
        geo = self.settings.value("geometry")
        if geo:
            self.restoreGeometry(geo)
        self.nav.setCurrentRow(0)
        self.update_dirty()

    # ---- state
    def show_page(self, i):
        self.stack.setCurrentIndex(i)
        if self.cp is not None:
            self.pages[i].refresh()

    def set_codeplug(self, cp, info=None):
        self.cp, self.radio_info = cp, info
        for p in self.pages:
            if isinstance(p, Page):
                p.selected()
        self.pages[self.nav.currentRow()].refresh()
        self.pages[0].refresh()
        self.update_dirty()

    def goto(self, page, key):
        """Jump to a record on another page (used by the right click menus)."""
        p = self.pages[page]
        self.nav.setCurrentRow(page)
        p.search.clear()
        p.table.clearSelection()
        p.select_key(key)

    def changed(self):
        self.timer.start(250)

    def update_dirty(self):
        s = self.cp.summary() if self.cp else {}
        n = sum(a + m + g for a, m, g in s.values())
        names = {"channels": "channels", "contacts": "contacts", "tglists": "TG lists", "zones": "zones", "identity": "identity"}
        self.dirty.setText(("Unsaved: " + ", ".join(
            "%s %s" % (names[k], "+%d ~%d -%d" % v) for k, v in s.items())) if n else
            ("No changes" if self.cp else "Read the radio to start"))
        self.b_write.setEnabled(bool(n) and self.thread is None)
        self.b_revert.setEnabled(bool(n))
        self.setWindowTitle("UV390 CPS%s" % (" *" if n else ""))

    def confirm_discard(self):
        if self.cp is None or not self.cp.summary():
            return True
        return QMessageBox.question(self, "Unsaved changes", "Discard the changes you made?") == QMessageBox.Yes

    def closeEvent(self, e):
        if not self.confirm_discard():
            e.ignore()
            return
        self.settings.setValue("geometry", self.saveGeometry())
        for t in ([self.thread] if self.thread else []) + self.finishing:
            t.wait(5000)
        e.accept()

    def revert(self):
        if self.cp and QMessageBox.question(self, "Revert", "Discard all changes?") == QMessageBox.Yes:
            self.set_codeplug(Codeplug(self.cp.img), self.radio_info)

    # ---- busy handling
    def busy(self, on, text=""):
        self.progress.setVisible(on)
        self.progress_text.setText(text)
        self.pages[0].read_btn.setEnabled(not on)
        self.b_write.setEnabled(False if on else self.b_write.isEnabled())
        if not on:
            t, self.thread = self.thread, None
            if t is not None:       # keep the QThread alive until run() has really returned
                self.finishing.append(t)
                t.finished.connect(lambda t=t: self.finishing.remove(t) if t in self.finishing else None)
            self.update_dirty()

    def on_progress(self, n, t, text=""):
        self.progress.setMaximum(max(t, 1))
        self.progress.setValue(n)
        if text:
            self.progress_text.setText(text)

    # ---- reading
    def port(self):
        port = self.pages[0].port_box.currentData()
        if not port:
            QMessageBox.warning(self, "No port", "No serial port found. Is the radio on and not in DFU or hotspot mode?")
        return port

    def read_radio(self):
        if self.thread or not self.confirm_discard():
            return
        port = self.port()
        if not port:
            return
        self.thread = ReadThread(port)
        self.busy(True, "Reading")
        self.thread.progress.connect(lambda n, t: self.on_progress(n, t))
        self.thread.done.connect(self.read_done)
        self.thread.failed.connect(self.read_failed)
        self.thread.start()

    def read_failed(self, msg):
        self.busy(False)
        QMessageBox.critical(self, "Read failed", msg)

    def read_done(self, img, info):
        self.busy(False)
        try:
            cp = Codeplug(img)
        except (KeyError, ValueError, IndexError) as e:
            QMessageBox.critical(self, "Parse error", "Codeplug could not be parsed: %s" % e)
            return
        self.set_codeplug(cp, info)

    # ---- writing
    def write_radio(self):
        if self.thread or self.cp is None:
            return
        problems = self.cp.problems()
        if problems:
            QMessageBox.warning(self, "Fix these first", "\n".join(problems[:15]))
            return
        new_img = self.cp.build_image()
        spans = self.cp.img.diff(new_img)
        if not spans:
            QMessageBox.information(self, "Nothing to write", "The radio already has this data.")
            return
        names = {"channels": "channels", "contacts": "contacts", "tglists": "TG lists", "zones": "zones", "identity": "identity"}
        lines = ["%s: %d added, %d changed, %d removed" % ((names[k],) + v) for k, v in self.cp.summary().items()]
        extra = "" if self.radio_info else "\n\nThe data comes from a backup file, not from this radio."
        box = QMessageBox(self)
        box.setIcon(QMessageBox.Question)
        box.setWindowTitle("Write to radio")
        box.setText("Write these changes to the radio?")
        box.setInformativeText("\n".join(lines) + "\n\nA backup of what is on the radio now is saved to\n%s\nbefore anything "
                               "is written. The radio restarts afterwards.%s" % (documents_dir(), extra))
        box.setStandardButtons(QMessageBox.Yes | QMessageBox.Cancel)
        box.button(QMessageBox.Yes).setText("Write")
        if box.exec_() != QMessageBox.Yes:
            return
        port = self.port()
        if not port:
            return
        self.pending_image = new_img
        self.thread = WriteThread(port, new_img, spans, documents_dir())
        self.busy(True, "Writing")
        self.thread.progress.connect(self.on_progress)
        self.thread.done.connect(self.write_done)
        self.thread.failed.connect(self.write_failed)
        self.thread.start()

    def write_failed(self, msg):
        self.busy(False)
        QMessageBox.critical(self, "Write failed", msg + "\n\nThe radio was restarted. Read it again before editing.")

    def write_done(self, backup):
        self.busy(False)
        info = self.radio_info
        self.set_codeplug(Codeplug(self.pending_image), info)
        QMessageBox.information(self, "Done", "Written and verified. The radio is restarting.\n\nBackup of the old data:\n%s" % backup)

    # ---- files
    def save_image(self):
        if not self.cp:
            return
        path, _ = QFileDialog.getSaveFileName(self, "Save backup", os.path.join(documents_dir(), "uv390-backup.json"),
                                              "Backup (*.json)")
        if path:
            self.cp.build_image().save(path)

    def open_image(self):
        if not self.confirm_discard():
            return
        path, _ = QFileDialog.getOpenFileName(self, "Open backup", documents_dir(), "Backup (*.json)")
        if path:
            try:
                self.set_codeplug(Codeplug(codeplug.Image.load(path)))
            except (OSError, ValueError, KeyError) as e:
                QMessageBox.critical(self, "Open failed", str(e))

    def export_csv(self, page):
        if not self.cp:
            return
        table = self.pages[page].table
        path, _ = QFileDialog.getSaveFileName(self, "Export CSV", "", "CSV (*.csv)")
        if not path:
            return
        with open(path, "w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow([table.horizontalHeaderItem(c).text() for c in range(table.columnCount())])
            for r in range(table.rowCount()):
                w.writerow([table.item(r, c).text() for c in range(table.columnCount())])


def main():
    app = QApplication(sys.argv)
    app.setStyle("Fusion")
    app.setStyleSheet(STYLE)
    w = MainWindow()
    w.show()
    sys.exit(app.exec_())
