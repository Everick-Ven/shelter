#include "app/browser/browser_controller.h"
#include <cassert>
int main(){shelter::BrowserController c;assert(c.tabs().Create("one","https://example.com"));assert(!c.tabs().Create("one","https://example.com"));assert(c.Navigate("one","https://example.org"));assert(!c.Navigate("one","javascript:bad"));assert(c.SetTitle("one","Example"));assert(c.tabs().Active()->title=="Example");assert(c.tabs().Create("two","https://example.net"));assert(c.tabs().Activate("one"));assert(c.tabs().Active()->id=="one");assert(c.tabs().Close("one"));assert(!c.tabs().Find("one"));}
