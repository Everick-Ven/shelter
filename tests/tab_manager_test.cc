#include "app/browser/tab_manager.h"
#include <cassert>
int main(){shelter::TabManager m;assert(m.Create("one","https://example.com"));assert(!m.Create("one","https://example.com"));assert(m.Create("two","https://example.net"));assert(m.Activate("one"));assert(m.Active()->id=="one");assert(m.Close("one"));assert(!m.Find("one"));}
