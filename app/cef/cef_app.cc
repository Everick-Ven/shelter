#include "app/cef/cef_app.h"
#include "app/cef/cef_client.h"
#include "app/cef/ui_scheme.h"
#include "app/common/logging.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"
#include "include/views/cef_window_delegate.h"
namespace shelter { void App::OnBeforeCommandLineProcessing(const CefString& t,CefRefPtr<CefCommandLine> c){if(t.empty()){c->AppendSwitch("disable-background-networking");c->AppendSwitch("no-default-browser-check");}} void App::OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> r){r->AddCustomScheme("shelter",CEF_SCHEME_OPTION_STANDARD|CEF_SCHEME_OPTION_SECURE|CEF_SCHEME_OPTION_CORS_ENABLED);}
void App::OnContextInitialized(){CEF_REQUIRE_UI_THREAD();Log(LogLevel::Info,"CEF context initialized");RegisterUiScheme();CefBrowserSettings s;auto tab=controller_.tabs().Create("main","shelter://ui/index.html"); auto v=CefBrowserView::CreateBrowserView(new Client(&controller_,tab->id),"https://example.com",s,nullptr,nullptr,nullptr);class D final:public CefWindowDelegate{CefRefPtr<CefBrowserView>v_;public:explicit D(CefRefPtr<CefBrowserView>v):v_(v){} bool IsFrameless(CefRefPtr<CefWindow>)override{return false;}void OnWindowCreated(CefRefPtr<CefWindow>w)override{w->SetTitle("SHELTER");w->SetToFillLayout();w->AddChildView(v_);w->Show();v_->RequestFocus();}private:IMPLEMENT_REFCOUNTING(D);};CefWindow::CreateTopLevelWindow(new D(v));} }
