# -*- coding: utf-8 -*-
import sys
import csv
import json
import random
from datetime import datetime
from collections import deque

from PyQt5.QtCore import Qt, QThread, pyqtSignal, QTimer
from PyQt5.QtWidgets import (
    QApplication,
    QComboBox,
    QFileDialog,
    QFormLayout,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QMainWindow,
    QMessageBox,
    QPushButton,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

import serial
import serial.tools.list_ports

from matplotlib.backends.backend_qt5agg import FigureCanvasQTAgg as FigureCanvas
from matplotlib.figure import Figure


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
    def __init__(self):
        super().__init__()
        self.setWindowTitle("水质上位机监测与分析平台")
        self.resize(1200, 760)

        self.reader = None
        self.sim_timer = QTimer(self)
        self.sim_timer.timeout.connect(self.generate_simulated_data)

        self.records = []
        self.series = deque(maxlen=60)

        self.metric_fields = [
            ("ph", "pH"),
            ("do", "溶氧(mg/L)"),
            ("turb", "浊度(NTU)"),
            ("temp", "温度(°C)"),
            ("cond", "电导率(uS/cm)"),
        ]

        self.metric_values = {key: 0.0 for key, _ in self.metric_fields}

        self.init_ui()
        self.refresh_ports()
        self.update_metrics_ui()
        self.init_chart()

    def init_ui(self):
        container = QWidget()
        main_layout = QVBoxLayout(container)

        top_layout = QHBoxLayout()
        main_layout.addLayout(top_layout)

        conn_group = QGroupBox("连接与采集")
        conn_layout = QVBoxLayout(conn_group)

        form_layout = QFormLayout()
        self.port_box = QComboBox()
        self.baud_box = QComboBox()
        self.baud_box.addItems(["9600", "115200", "256000"])
        self.baud_box.setCurrentText("115200")
        form_layout.addRow("串口", self.port_box)
        form_layout.addRow("波特率", self.baud_box)
        conn_layout.addLayout(form_layout)

        btn_row = QHBoxLayout()
        self.refresh_btn = QPushButton("刷新串口")
        self.connect_btn = QPushButton("连接")
        self.disconnect_btn = QPushButton("断开")
        self.disconnect_btn.setEnabled(False)
        self.simulate_btn = QPushButton("模拟数据")
        btn_row.addWidget(self.refresh_btn)
        btn_row.addWidget(self.connect_btn)
        btn_row.addWidget(self.disconnect_btn)
        btn_row.addWidget(self.simulate_btn)
        conn_layout.addLayout(btn_row)

        self.status_label = QLabel("状态：未连接")
        self.status_label.setStyleSheet("color:#475569")
        conn_layout.addWidget(self.status_label)

        self.refresh_btn.clicked.connect(self.refresh_ports)
        self.connect_btn.clicked.connect(self.connect_serial)
        self.disconnect_btn.clicked.connect(self.disconnect_serial)
        self.simulate_btn.clicked.connect(self.toggle_simulation)

        metrics_group = QGroupBox("实时水质参数")
        metrics_layout = QGridLayout(metrics_group)
        self.metric_labels = {}
        for idx, (key, label) in enumerate(self.metric_fields):
            row = idx // 2
            col = (idx % 2) * 2
            name_label = QLabel(label)
            value_label = QLabel("--")
            value_label.setStyleSheet("font-size:18px;font-weight:600;color:#0ea5e9")
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

        self.table = QTableWidget(0, 7)
        self.table.setHorizontalHeaderLabels([
            "时间", "pH", "溶氧", "浊度", "温度", "电导率", "原始帧"
        ])
        self.table.verticalHeader().setVisible(False)
        self.table.setEditTriggers(QTableWidget.NoEditTriggers)
        self.table.setAlternatingRowColors(True)
        table_layout.addWidget(self.table)

        btn_row2 = QHBoxLayout()
        self.clear_btn = QPushButton("清空记录")
        self.export_btn = QPushButton("导出CSV")
        self.record_count = QLabel("记录条数：0")
        btn_row2.addWidget(self.clear_btn)
        btn_row2.addWidget(self.export_btn)
        btn_row2.addStretch(1)
        btn_row2.addWidget(self.record_count)
        table_layout.addLayout(btn_row2)

        self.clear_btn.clicked.connect(self.clear_records)
        self.export_btn.clicked.connect(self.export_csv)

        main_layout.addWidget(chart_group, 2)
        main_layout.addWidget(table_group, 3)

        self.setCentralWidget(container)

    def refresh_ports(self):
        self.port_box.clear()
        ports = serial.tools.list_ports.comports()
        for port in ports:
            self.port_box.addItem(port.device)
        if not ports:
            self.port_box.addItem("未检测到串口")

    def connect_serial(self):
        if self.reader and self.reader.isRunning():
            return
        port = self.port_box.currentText()
        if not port or port == "未检测到串口":
            QMessageBox.warning(self, "提示", "请先连接串口设备。")
            return
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

        parts = [p.strip() for p in line.split(",") if p.strip()]
        if not parts:
            return None

        if any(":" in part for part in parts):
            payload = {}
            for part in parts:
                if ":" not in part:
                    continue
                key, value = part.split(":", 1)
                payload[key.strip()] = value.strip()
            return self.normalize_payload(payload)

        if len(parts) >= 5:
            try:
                values = [float(v) for v in parts[:5]]
            except ValueError:
                return None
            return {
                "ph": values[0],
                "do": values[1],
                "turb": values[2],
                "temp": values[3],
                "cond": values[4],
            }
        return None

    def normalize_payload(self, payload):
        mapping = {
            "ph": "ph",
            "PH": "ph",
            "do": "do",
            "DO": "do",
            "turb": "turb",
            "TURB": "turb",
            "temp": "temp",
            "TEMP": "temp",
            "cond": "cond",
            "COND": "cond",
        }
        data = {}
        for key, value in payload.items():
            if key not in mapping:
                continue
            try:
                data[mapping[key]] = float(value)
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
            "do": self.metric_values["do"],
            "turb": self.metric_values["turb"],
            "temp": self.metric_values["temp"],
            "cond": self.metric_values["cond"],
            "raw": raw,
        }
        self.records.append(record)
        self.record_count.setText(f"记录条数：{len(self.records)}")

        self.add_table_row(record)
        self.series.append(record)
        self.draw_chart()

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
            f"{record['do']:.2f}",
            f"{record['turb']:.2f}",
            f"{record['temp']:.2f}",
            f"{record['cond']:.1f}",
            record["raw"],
        ]
        for col, value in enumerate(values):
            item = QTableWidgetItem(value)
            item.setTextAlignment(Qt.AlignCenter if col != 6 else Qt.AlignLeft)
            self.table.setItem(row, col, item)
        if self.table.rowCount() > 200:
            self.table.removeRow(0)

    def clear_records(self):
        self.records.clear()
        self.series.clear()
        self.table.setRowCount(0)
        self.record_count.setText("记录条数：0")
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
                writer.writerow(["time", "ph", "do", "turb", "temp", "cond", "raw"])
                for r in self.records:
                    writer.writerow([
                        r["time"],
                        f"{r['ph']:.2f}",
                        f"{r['do']:.2f}",
                        f"{r['turb']:.2f}",
                        f"{r['temp']:.2f}",
                        f"{r['cond']:.1f}",
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
            "do": self.ax.plot([], [], label="溶氧", color="#22c55e")[0],
            "turb": self.ax.plot([], [], label="浊度", color="#f97316")[0],
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
        for key in ("ph", "do", "turb"):
            y_vals = [r[key] for r in self.series]
            self.lines[key].set_data(x_vals, y_vals)
        self.ax.relim()
        self.ax.autoscale_view()
        self.ax.set_xlim(0, max(60, len(self.series)))
        self.canvas.draw_idle()

    def generate_simulated_data(self):
        base = {
            "ph": 7.0,
            "do": 6.0,
            "turb": 5.0,
            "temp": 24.0,
            "cond": 420.0,
        }
        data = {}
        for key, val in base.items():
            jitter = random.uniform(-0.3, 0.3)
            data[key] = max(0.1, val + jitter)
        raw = ",".join([
            f"PH:{data['ph']:.2f}",
            f"DO:{data['do']:.2f}",
            f"TURB:{data['turb']:.2f}",
            f"TEMP:{data['temp']:.2f}",
            f"COND:{data['cond']:.1f}",
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
    window = WaterQualityHost()
    window.show()
    sys.exit(app.exec_())


if __name__ == "__main__":
    main()
