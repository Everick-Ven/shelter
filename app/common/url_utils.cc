#include "app/common/url_utils.h"
namespace shelter { bool IsValidNavigationUrl(std::string_view u){if(u.empty()||u.find_first_of("\r\n")!=u.npos)return false;auto p=u.find("://");if(p==u.npos)return false;auto s=u.substr(0,p);return s=="http"||s=="https"||s=="shelter";} }
