#include "app.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--demo") == 0) {
            setenv("POCKETALERT_DEMO", "1", 1);
            // Keep demo settings away from a real signed-in config.
            setenv("PA_CARDPUTER_CONFIG_DIR", "/tmp/pocketalert-cardputer-demo", 0);
        } else if (std::strcmp(argv[i], "--version") == 0) {
            std::printf("pocketalert-cardputer %s\n", PA_VERSION);
            return 0;
        }
    }
    app::Application application;
    return application.run();
}
