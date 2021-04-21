#ifndef TYPES_H_
enum StateEnum {
  STATE_INVALID,
  STATE_OFF,
	STATE_BROADCAST,
	STATE_SLEEP,
	STATE_ENSURE_DOWN,
	STATE_TRANSMIT,
	STATE_ENSURE_UP,
};

struct StateContainer {
  StateEnum state;
	uint32_t stateEnteredMs;
};

enum Event : uint32_t {
  EVENT_NONE = uint32_t(0),
	EVENT_BUTTON_UP = (1u << 0),
  EVENT_BUTTON_DOWN = (1u << 2),
	EVENT_CONNECTED = (1u << 3),
	EVENT_HOURS_3 = (1u << 4),
	EVENT_MILLIS_10 = (1u << 5),
};
#define TYPES_H_
#endif  // TYPES_H_
