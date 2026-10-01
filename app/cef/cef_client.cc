#include "app/cef/cef_client.h"
#include "app/common/logging.h"
namespace shelter { void Client::OnTitleChange(CefRefPtr<CefBrowser>,const CefString& t){Log(LogLevel::Info,std::string("title: ")+t.ToString());} void Client::OnLoadError(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>,ErrorCode e,const CefString&,const CefString&){Log(LogLevel::Warning,"navigation error: "+std::to_string((int)e));} }
