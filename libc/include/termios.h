struct termios {
    unsigned int c_iflag, c_oflag, c_cflag, c_lflag;
    unsigned char c_cc[20];
};
#define TCSANOW   0
#define TCSADRAIN 1
#define ECHO  0x8
#define ICANON 0x2
int tcgetattr(int fd, struct termios *t);
int tcsetattr(int fd, int action, const struct termios *t);
