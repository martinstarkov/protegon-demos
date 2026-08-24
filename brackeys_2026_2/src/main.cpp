#include "app/application.h"
#include "app/editor.h"

using namespace ptgn;

int main(int, char**) {
	Application app{ "Brackeys 2026_2" };
	PTGN_WITH_EDITOR(app, true);
	app.StartProject();
}