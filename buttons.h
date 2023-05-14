enum User{driver, passenger};
enum SwitchDirection{up, down};

struct Button {
	enum User user;
	enum SwitchDirection dir;
};