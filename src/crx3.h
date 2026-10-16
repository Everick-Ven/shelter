// SHELTER — проверка подписи CRX3 системной криптографией.
#ifndef SHELTER_CRX3_H_
#define SHELTER_CRX3_H_

#include <cstddef>
#include <string>

namespace shelter {
namespace crx3 {

// Проверяет структуру CRX3, CRX ID, подпись ZIP-данных и ожидаемый ID магазина.
bool Verify(const std::string& bytes, const std::string& expected_id,
            std::size_t* zip_offset, std::string* extension_id);

}  // namespace crx3
}  // namespace shelter

#endif  // SHELTER_CRX3_H_
