// Контракт src/download_name.h: имя файла из сети обязано стать безопасным
// компонентом пути в папке «Загрузки» — без обхода каталога, ADS-потоков,
// имён устройств Windows и рассинхрона «показали/записали».
#include <iostream>
#include <string>

#include "src/download_name.h"

namespace {

int Failures = 0;

void CheckName(const std::string& input, const std::string& expected) {
  const std::string got = shelter::SanitizeDownloadLeaf(input);
  if (got != expected) {
    std::cerr << "FAIL: SanitizeDownloadLeaf(\"" << input << "\") = \"" << got
              << "\", ожидалось \"" << expected << "\"" << std::endl;
    ++Failures;
  }
}

void Check(bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAIL: " << message << std::endl;
  ++Failures;
}

// Проверяет, что строка — валидный UTF-8 (для теста усечения).
bool IsValidUtf8(const std::string& s) {
  size_t i = 0;
  while (i < s.size()) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    size_t want = 0;
    if (c < 0x80) {
      want = 1;
    } else if ((c & 0xE0) == 0xC0) {
      want = 2;
    } else if ((c & 0xF0) == 0xE0) {
      want = 3;
    } else if ((c & 0xF8) == 0xF0) {
      want = 4;
    } else {
      return false;
    }
    if (i + want > s.size()) return false;
    for (size_t k = 1; k < want; ++k) {
      if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
    }
    i += want;
  }
  return true;
}

}  // namespace

int main() {
  // Обычные имена не меняются (включая Unicode).
  CheckName("doc.pdf", "doc.pdf");
  CheckName("отчёт 2026 финал (2).PDF", "отчёт 2026 финал (2).PDF");
  CheckName("archive.tar.gz", "archive.tar.gz");

  // Обход каталога невозможен.
  CheckName("../../etc/passwd", "passwd");
  CheckName("..\\..\\win\\x.exe", "x.exe");
  CheckName("a/b\\c.txt", "c.txt");
  CheckName("/abs/path/f", "f");

  // Запрещённые символы и управляющие коды.
  CheckName("report:final.pdf", "report_final.pdf");
  CheckName("a<b>c|d?e*f\"g.pdf", "a_b_c_d_e_f_g.pdf");
  CheckName("a\tb\nc.pdf", "a_b_c.pdf");
  CheckName("file\x7f.pdf", "file_.pdf");

  // Завершающие точки/пробелы и ведущие пробелы (рассинхрон Windows).
  CheckName("file.", "file");
  CheckName("file...   ", "file");
  CheckName("   spaced   ", "spaced");

  // Зарезервированные имена устройств Windows.
  CheckName("nul", "_nul");
  CheckName("NUL", "_NUL");
  CheckName("con.txt", "_con.txt");
  CheckName("COM1", "_COM1");
  CheckName("lpt9.dat", "_lpt9.dat");
  CheckName("aux.pdf", "_aux.pdf");
  CheckName("com1.txt.exe", "_com1.txt.exe");
  CheckName("my.com1", "my.com1");
  CheckName("console", "console");

  // Пустое и вырожденное.
  CheckName("", "download");
  CheckName("...", "download");
  CheckName("   ", "download");

  // Усечение длинных имён: в пределах лимита и валидный UTF-8.
  const std::string ascii_long(300, 'a');
  const std::string ascii_cut = shelter::SanitizeDownloadLeaf(ascii_long);
  Check(ascii_cut.size() == shelter::kMaxDownloadLeafBytes,
        "длинное ASCII-имя усечено до лимита");
  std::string wide;
  for (int i = 0; i < 100; ++i) wide += "\xd1\x84";  // 'ф', 2 байта
  wide += ".pdf";
  const std::string wide_cut = shelter::SanitizeDownloadLeaf(wide);
  Check(wide_cut.size() <= shelter::kMaxDownloadLeafBytes,
        "длинное Unicode-имя усечено до лимита");
  Check(IsValidUtf8(wide_cut), "усечённое имя — валидный UTF-8");

  if (Failures == 0) std::cout << "download-name-test: OK" << std::endl;
  return Failures ? 1 : 0;
}
