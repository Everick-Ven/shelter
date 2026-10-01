#include "app/cef/ui_scheme.h"
#include "include/cef_parser.h"
#include "include/cef_resource_handler.h"
#include "include/cef_stream.h"
#include <fstream>
#include <cstring>
#include <algorithm>
#include <iterator>
namespace shelter { namespace {
class Handler final:public CefResourceHandler{std::string data_;size_t off_=0;public:explicit Handler(std::string d):data_(std::move(d)){}bool ProcessRequest(CefRefPtr<CefRequest> r,CefRefPtr<CefCallback> cb)override{cb->Continue();return true;}void GetResponseHeaders(CefRefPtr<CefResponse> r,int64_t& n,CefString&)override{r->SetStatus(200);r->SetMimeType(data_.find("function") != std::string::npos ? "application/javascript" : "text/html");n=data_.size();}bool ReadResponse(void* b,int s,int& n,CefRefPtr<CefResourceSkipCallback>)override{n=0;if(off_>=data_.size())return false;size_t k=std::min<size_t>(s,data_.size()-off_);memcpy(b,data_.data()+off_,k);off_+=k;n=(int)k;return true;}bool Skip(int64_t,int64_t& s,CefRefPtr<CefResourceSkipCallback>)override{s=0;return false;}bool ProcessRequest(CefRefPtr<CefRequest>,CefRefPtr<CefCallback>,CefRefPtr<CefResourceHandler>) {return false;}private:IMPLEMENT_REFCOUNTING(Handler);};
class Factory final:public CefSchemeHandlerFactory{public:CefRefPtr<CefResourceHandler>Create(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>,const CefString&,CefRefPtr<CefRequest> request)override{std::string path=request->GetURL().ToString().find("host-bridge.js")!=std::string::npos?"resources/ui/host-bridge.js":"resources/ui/index.html";std::ifstream f(path,std::ios::binary);return new Handler(std::string((std::istreambuf_iterator<char>(f)),{}));}private:IMPLEMENT_REFCOUNTING(Factory);};}
void RegisterUiScheme(){CefRegisterSchemeHandlerFactory("shelter","ui",new Factory);}
}
