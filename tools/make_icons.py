#!/usr/bin/env python3
"""Раскладывает КАНОНИЧЕСКИЕ иконки из resources/icons/ по точкам сборки.

Иконка SHELTER — зафиксированный ассет бренда (чёрная плитка + белая «S»).
Контур «S», пропорции, цвета и скругления НЕ генерируются и НЕ изменяются:
файлы только копируются/пересобираются из resources/icons/* (см. PR-правило
«элементы иконки и название не меняются без прямого указания»).
"""
import os
import shutil

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "resources", "icons")


def main():
    # Окно приложения (shell читает icon-256.png) — байт-в-байт канон.
    os.makedirs(os.path.join(ROOT, "resources", "ui"), exist_ok=True)
    shutil.copyfile(os.path.join(SRC, "icon-256.png"),
                    os.path.join(ROOT, "resources", "ui", "icon-256.png"))

    # Windows: канонический .ico байт-в-байт (exe + установщик).
    shutil.copyfile(os.path.join(SRC, "SHELTER.ico"),
                    os.path.join(ROOT, "win", "shelter.ico"))

    # macOS: .icns пересобирается из канонического 1024×1024.
    src = Image.open(os.path.join(SRC, "icon-1024.png")).convert("RGBA")
    src.save(os.path.join(ROOT, "mac", "Shelter.icns"), format="ICNS",
             sizes=[(16, 16), (32, 32), (64, 64), (128, 128),
                    (256, 256), (512, 512)])
    print("ok (canon repack)")


main()
