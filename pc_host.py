# -*- coding: utf-8 -*-
import sys
import csv
import json
import random
from datetime import datetime
from collections import deque

from PyQt5.QtCore import Qt, QThread, pyqtSignal, QTimer
from PyQt5.QtGui import QFont, QFontDatabase
from PyQt5.QtWidgets import (
    QApplication,
    QComboBox,
    QFileDialog,
    QFormLayout,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMainWindow,
    QMessageBox,
    QPlainTextEdit,
    QPushButton,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

import serial
import serial.tools.list_ports

import matplotlib as mpl
from matplotlib.backends.backend_qt5agg import FigureCanvasQTAgg as FigureCanvas
from matplotlib.figure import Figure

TURB_MAX_TU = 3000.0


class SerialReader(QThread):
    line_received = pyqtSignal(str)
    status = pyqtSignal(str)
    error = pyqtSignal(str)

    def __init__(self, port, baudrate, parent=None):
        super().__init__(parent)
        self.port = port
        self.baudrate = baudrate
        self._running = False
        self._ser = None

    def run(self):
        try:
            self._ser = serial.Serial(self.port, self.baudrate, timeout=1)
            self.status.emit(f"已连接 {self.port} @ {self.baudrate}")
            self._running = True
            while self._running:
                try:
                    raw = self._ser.readline()
                except serial.SerialException as exc:
                    self.error.emit(f"串口读取失败：{exc}")
                    break
                if raw:
                    line = raw.decode(errors="ignore").strip()
                    if line:
                        self.line_received.emit(line)
        except (serial.SerialException, OSError) as exc:
            self.error.emit(f"串口打开失败：{exc}")
        finally:
            if self._ser:
                try:
                    self._ser.close()
                except serial.SerialException:
                    pass
            self.status.emit("已断开")

    def stop(self):
        self._running = False
        if self._ser and self._ser.is_open:
            try:
                self._ser.close()
            except serial.SerialException:
                pass


class WaterQualityHost(QMainWindow):
    def __init__( self):
        super().__init__()
        self.setWindowTitle("水质上位机监控界面")
        self.resize(1100, 720)

        self.reader = None
        self.sim_timer = QTimer(self)
        self.sim_timer.timeout.connect(self.generate_simulated_data)

        self.records = []
        self.series = deque(maxlen=60)
        self.raw_lines = deque(maxlen=8)

        self.metric_fields = [
            ("ph", "pH"),
            ("temp", "TEMP(°C)"),
            ("tds", "TDS(ppm)"),
            ("turb", "TU(%)"),
        ]

        self.metric_values = {key: 0.0 for key, _ in self.metric_fields}


        self.init_ui()
        self.refresh_ports()
        self.update_metrics_ui()
        self.init_chart()

    def init_ui(self):
        container = QWidget()
        main_layout = QVBoxLayout(container)
        main_layout.setSpacing(10)
        main_layout.setContentsMargins(12, 12, 12, 12)

        self.setStyleSheet(
            """
            QGroupBox {
                border: 1px solid #d1d5db;
                border-radius: 8px;
                margin-top: 10px;
                font-weight: 600;
            }
            QGroupBox::title {
                subcontrol-origin: margin;
                left: 10px;
                padding: 0 4px;
                color: #111827;
            }
            QLabel {
                color: #111827;
            }
            QLabel#MutedText {
                color: #6b7280;
            }
            QPushButton {
                padding: 6px 10px;
                border-radius: 6px;
            }
            QPushButton#PrimaryBtn {
                background: #0f766e;
                color: #ffffff;
            }
            QPushButton#WarnBtn {
                background: #f97316;
                color: #ffffff;
            }
            QTableWidget {
                gridline-color: #e5e7eb;
            }
            """
        )

        top_layout = QHBoxLayout()
        main_layout.addLayout(top_layout)

        conn_group = QGroupBox("连接与控制")
        conn_layout = QVBoxLayout(conn_group)

        form_layout = QFormLayout()
        self.port_box = QComboBox()
        self.manual_port = QLineEdit()
        self.manual_port.setPlaceholderText("手动输入端口，如 /dev/cu.usbserial 或 COM3")
        self.manual_port.setClearButtonEnabled(True)
        self.baud_box = QComboBox()
        self.baud_box.addItems(["9600", "115200", "256000"])
        self.baud_box.setCurrentText("115200")
        form_layout.addRow("串口", self.port_box)
        form_layout.addRow("手动端口", self.manual_port)
        form_layout.addRow("波特率", self.baud_box)
        conn_layout.addLayout(form_layout)

        btn_row = QHBoxLayout()
        self.refresh_btn = QPushButton("刷新串口")
        self.connect_btn = QPushButton("连接")
        self.disconnect_btn = QPushButton("断开")
        self.disconnect_btn.setEnabled(False)
        self.simulate_btn = QPushButton("模拟数据")
        self.connect_btn.setObjectName("PrimaryBtn")
        self.simulate_btn.setObjectName("WarnBtn")
        btn_row.addWidget(self.refresh_btn)
        btn_row.addWidget(self.connect_btn)
        btn_row.addWidget(self.disconnect_btn)
        btn_row.addWidget(self.simulate_btn)
        conn_layout.addLayout(btn_row)

        self.status_label = QLabel("状态：未连接")
        self.status_label.setObjectName("MutedText")
        self.status_label.setStyleSheet("color:#6b7280")
        self.info_label = QLabel("数据间隔：1s | 保存路径：./data/logs/")
        self.info_label.setObjectName("MutedText")
        self.info_label.setStyleSheet("color:#6b7280")
        conn_layout.addWidget(self.status_label)
        conn_layout.addWidget(self.info_label)

        self.refresh_btn.clicked.connect(self.refresh_ports)
        self.connect_btn.clicked.connect(self.connect_serial)
        self.disconnect_btn.clicked.connect(self.disconnect_serial)
        self.simulate_btn.clicked.connect(self.toggle_simulation)

        metrics_group = QGroupBox("实时参数")
        metrics_layout = QGridLayout(metrics_group)
        self.metric_labels = {}
        for idx, (key, label) in enumerate(self.metric_fields):
            row = idx // 2
            col = (idx % 2) * 2
            name_label = QLabel(label)
            value_label = QLabel("--")
            value_label.setStyleSheet("font-size:18px;font-weight:600;color:#0f766e")
            self.metric_labels[key] = value_label
            metrics_layout.addWidget(name_label, row, col)
            metrics_layout.addWidget(value_label, row, col + 1)

        top_layout.addWidget(conn_group, 1)
        top_layout.addWidget(metrics_group, 2)

        chart_group = QGroupBox("趋势曲线")
        chart_layout = QVBoxLayout(chart_group)
        self.figure = Figure(figsize=(6, 3))
        self.canvas = FigureCanvas(self.figure)
        chart_layout.addWidget(self.canvas)

        table_group = QGroupBox("记录与导出")
        table_layout = QVBoxLayout(table_group)

        self.table = QTableWidget(0, 6)
        self.table.setHorizontalHeaderLabels([
            "时间", "pH", "温度", "TDS", "浊度(%)", "原始帧"
        ])
        self.table.verticalHeader().setVisible(False)
        self.table.setEditTriggers(QTableWidget.NoEditTriggers)
        self.table.setAlternatingRowColors(True)
        self.table.horizontalHeader().setStretchLastSection(True)
        table_layout.addWidget(self.table)

        btn_row2 = QHBoxLayout()
        self.clear_btn = QPushButton("清空记录")
        self.export_btn = QPushButton("导出CSV")
        self.export_btn.setObjectName("PrimaryBtn")
        self.record_count = QLabel("记录条数：0")
        self.record_count.setObjectName("MutedText")
        self.record_count.setStyleSheet("color:#6b7280")
        btn_row2.addWidget(self.clear_btn)
        btn_row2.addWidget(self.export_btn)
        btn_row2.addStretch(1)
        btn_row2.addWidget(self.record_count)
        table_layout.addLayout(btn_row2)

        raw_box = QHBoxLayout()
        raw_label = QLabel("原始数据缓冲（最近 8 行）")
        raw_label.setObjectName("MutedText")
        raw_label.setStyleSheet("color:#6b7280")
        raw_box.addWidget(raw_label)
        table_layout.addLayout(raw_box)

        self.raw_log = QPlainTextEdit()
        self.raw_log.setReadOnly(True)
        self.raw_log.setFixedHeight(90)
        self.raw_log.setStyleSheet("background:#111827;color:#e5e7eb;border-radius:6px;")
        table_layout.addWidget(self.raw_log)

        self.clear_btn.clicked.connect(self.clear_records)
        self.export_btn.clicked.connect(self.export_csv)

        main_layout.addWidget(chart_group, 2)
        main_layout.addWidget(table_group, 3)

        self.setCentralWidget(container)

    def refresh_ports(self):
        self.port_box.clear()
        ports = serial.tools.list_ports.comports()
        for port in ports:
            desc = port.description or "未知设备"
            hwid = port.hwid or "HWID?"
            label = f"{port.device} | {desc} | {hwid}"
            self.port_box.addItem(label)
        if not ports:
            self.port_box.addItem("未检测到串口")

    def connect_serial(self):
        if self.reader and self.reader.isRunning():
            return
        manual = self.manual_port.text().strip()
        if manual:
            port = manual
        else:
            port = self.port_box.currentText()
        if not port or port == "未检测到串口":
            QMessageBox.warning(self, "提示", "请先连接串口设备。")
            return
        if "|" in port:
            port = port.split("|", 1)[0].strip()
        try:
            baud = int(self.baud_box.currentText())
        except ValueError:
            baud = 115200
        self.reader = SerialReader(port, baud)
        self.reader.line_received.connect(self.handle_line)
        self.reader.status.connect(self.set_status)
        self.reader.error.connect(self.show_error)
        self.reader.start()
        self.connect_btn.setEnabled(False)
        self.disconnect_btn.setEnabled(True)
        self.simulate_btn.setEnabled(False)

    def disconnect_serial(self):
        if self.reader:
            self.reader.stop()
            self.reader.wait(1000)
            self.reader = None
        self.set_status("未连接")
        self.connect_btn.setEnabled(True)
        self.disconnect_btn.setEnabled(False)
        self.simulate_btn.setEnabled(True)

    def toggle_simulation(self):
        if self.sim_timer.isActive():
            self.sim_timer.stop()
            self.set_status("模拟停止")
            self.connect_btn.setEnabled(True)
            self.disconnect_btn.setEnabled(False)
        else:
            self.sim_timer.start(1000)
            self.set_status("模拟运行中")
            self.connect_btn.setEnabled(False)
            self.disconnect_btn.setEnabled(True)

    def show_error(self, msg):
        QMessageBox.critical(self, "错误", msg)
        self.disconnect_serial()

    def set_status(self, msg):
        self.status_label.setText(f"状态：{msg}")

    def handle_line(self, line):
        data = self.parse_line(line)
        if data:
            self.update_data(data, line)

    def parse_line(self, line):
        line = line.strip()
        if not line:
            return None
        if line.startswith("{"):
            try:
                payload = json.loads(line)
                return self.normalize_payload(payload)
            except json.JSONDecodeError:
                return None

        normalized = line.replace(";", ",")
        parts = [p.strip() for p in normalized.split(",") if p.strip()]
        if not parts:
            return None

        if any((":" in part or "=" in part) for part in parts):
            payload = {}
            for part in parts:
                if ":" in part:
                    key, value = part.split(":", 1)
                elif "=" in part:
                    key, value = part.split("=", 1)
                else:
                    continue
                payload[key.strip()] = value.strip()
            return self.normalize_payload(payload)

        if len(parts) >= 4:
            try:
                values = [float(v) for v in parts[:4]]
            except ValueError:
                return None
            return {
                "ph": values[0],
                "temp": values[1],
                "tds": values[2],
                "turb": max(0.0, min(100.0, values[3] * 100.0 / TURB_MAX_TU)),
            }
        return None

    def normalize_payload(self, payload):
        mapping = {
            "ph": "ph",
            "PH": "ph",
            "tds": "tds",
            "TDS": "tds",
            "turb": "turb",
            "TURB": "turb",
            "TU": "turb",
            "temp": "temp",
            "TEMP": "temp",
        }
        data = {}
        for key, value in payload.items():
            if key not in mapping:
                continue
            try:
                parsed = float(value)
                if mapping[key] == "turb":
                    parsed = max(0.0, min(100.0, parsed * 100.0 / TURB_MAX_TU))
                data[mapping[key]] = parsed
            except ValueError:
                continue
        if len(data) < 3:
            return None
        return data

    def update_data(self, data, raw):
        for key in self.metric_values.keys():
            if key in data:
                self.metric_values[key] = data[key]
        self.update_metrics_ui()

        timestamp = datetime.now().strftime("%H:%M:%S")
        record = {
            "time": timestamp,
            "ph": self.metric_values["ph"],
            "temp": self.metric_values["temp"],
            "tds": self.metric_values["tds"],
            "turb": self.metric_values["turb"],
            "raw": raw,
        }
        self.records.append(record)
        self.record_count.setText(f"记录条数：{len(self.records)}")

        self.add_table_row(record)
        self.series.append(record)
        self.draw_chart()
        self.update_raw_log(raw)

    def update_metrics_ui(self):
        for key, label in self.metric_labels.items():
            value = self.metric_values.get(key, 0.0)
            label.setText(f"{value:.2f}")

    def add_table_row(self, record):
        row = self.table.rowCount()
        self.table.insertRow(row)
        values = [
            record["time"],
            f"{record['ph']:.2f}",
            f"{record['temp']:.2f}",
            f"{record['tds']:.0f}",
            f"{record['turb']:.1f}",
            record["raw"],
        ]
        for col, value in enumerate(values):
            item = QTableWidgetItem(value)
            item.setTextAlignment(Qt.AlignCenter if col != 5 else Qt.AlignLeft)
            self.table.setItem(row, col, item)
        if self.table.rowCount() > 200:
            self.table.removeRow(0)

    def clear_records(self):
        self.records.clear()
        self.series.clear()
        self.table.setRowCount(0)
        self.record_count.setText("记录条数：0")
        self.raw_lines.clear()
        self.raw_log.setPlainText("")
        self.draw_chart()

    def export_csv(self):
        if not self.records:
            QMessageBox.information(self, "提示", "暂无数据可导出。")
            return
        path, _ = QFileDialog.getSaveFileName(self, "导出CSV", "water_quality.csv", "CSV Files (*.csv)")
        if not path:
            return
        try:
            with open(path, "w", newline="", encoding="utf-8") as f:
                writer = csv.writer(f)
                writer.writerow(["time", "ph", "temp", "tds", "turb_percent", "raw"])
                for r in self.records:
                    writer.writerow([
                        r["time"],
                        f"{r['ph']:.2f}",
                        f"{r['temp']:.2f}",
                        f"{r['tds']:.0f}",
                        f"{r['turb']:.1f}",
                        r["raw"],
                    ])
            QMessageBox.information(self, "完成", "导出成功。")
        except OSError as exc:
            QMessageBox.critical(self, "错误", f"保存失败：{exc}")

    def init_chart(self):
        self.ax = self.figure.add_subplot(111)
        self.ax.set_title("水质参数趋势")
        self.ax.set_xlabel("样本")
        self.ax.set_ylabel("数值")
        self.lines = {
            "ph": self.ax.plot([], [], label="pH", color="#0ea5e9")[0],
            "temp": self.ax.plot([], [], label="温度", color="#f97316")[0],
            "tds": self.ax.plot([], [], label="TDS", color="#22c55e")[0],
            "turb": self.ax.plot([], [], label="浊度(%)", color="#a855f7")[0],
        }
        self.ax.legend(loc="upper right")
        self.figure.tight_layout()

    def draw_chart(self):
        if not self.series:
            self.ax.set_xlim(0, 60)
            self.ax.set_ylim(0, 10)
            self.canvas.draw_idle()
            return
        x_vals = list(range(len(self.series)))
        for key in ("ph", "temp", "tds", "turb"):
            y_vals = [r[key] for r in self.series]
            self.lines[key].set_data(x_vals, y_vals)
        self.ax.relim()
        self.ax.autoscale_view()
        self.ax.set_xlim(0, max(60, len(self.series)))
        self.canvas.draw_idle()

    def update_raw_log(self, raw):
        self.raw_lines.append(raw)
        self.raw_log.setPlainText("\n".join(self.raw_lines))

    def generate_simulated_data(self):
        base = {
            "ph": 7.0,
            "temp": 24.0,
            "tds": 300.0,
            "turb": 5.0,
        }
        data = {}
        for key, val in base.items():
            jitter = random.uniform(-0.3, 0.3)
            data[key] = max(0.1, val + jitter)
        raw = ",".join([
            f"PH:{data['ph']:.2f}",
            f"TEMP:{data['temp']:.2f}",
            f"TDS:{data['tds']:.0f}",
            f"TURB:{data['turb']:.2f}",
        ])
        self.update_data(data, raw)

    def closeEvent(self, event):
        if self.reader:
            self.reader.stop()
            self.reader.wait(1000)
        if self.sim_timer.isActive():
            self.sim_timer.stop()
        event.accept()


def main():
    app = QApplication(sys.argv)
    families = set(QFontDatabase().families())
    candidates = [
        "PingFang SC",
        "Hiragino Sans GB",
        "Heiti SC",
        "STHeiti",
        "Songti SC",
        "Arial Unicode MS",
    ]
    available = [name for name in candidates if name in families]
    selected = available[0] if available else None
    if selected:
        app.setFont(QFont(selected, 10))
    mpl.rcParams["font.family"] = "sans-serif"
    mpl.rcParams["font.sans-serif"] = (available + ["DejaVu Sans"])
    mpl.rcParams["axes.unicode_minus"] = False
    window = WaterQualityHost()
    window.show()
    sys.exit(app.exec_())


if __name__ == "__main__":
    main()
