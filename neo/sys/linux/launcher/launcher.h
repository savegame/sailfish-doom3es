#ifndef D3ES_LAUNCHER
#define D3ES_LAUNCHER

class LauncherPrivate;
class Launcher {
public:
    enum Status {
        Ok = 0,
        Quit = 1,
        Error
    };
    Launcher(int argc, char** argv);
    ~Launcher();

    int argc() const;
    const char** argv();

    int render();
private: 
    LauncherPrivate *d_ptr = nullptr;
};
#endif