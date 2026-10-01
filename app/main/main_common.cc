#include "app/cef/cef_app.h"
#include "app/common/logging.h"
#include "include/cef_app.h"
#if defined(_WIN32)
#include <windows.h>
#endif
int main(int argc,char**argv){shelter::InitializeLogging("shelter.log");#if defined(_WIN32)
  CefMainArgs a(GetModuleHandle(nullptr));
#else
  CefMainArgs a(argc,argv);
#endifCefRefPtr<shelter::App> app(new shelter::App);int r=CefExecuteProcess(a,app,nullptr);if(r>=0)return r;CefSettings s;s.no_sandbox=true;CefString(&s.log_file)="shelter-cef.log";if(!CefInitialize(a,s,app,nullptr))return 1;CefRunMessageLoop();CefShutdown();return 0;}
