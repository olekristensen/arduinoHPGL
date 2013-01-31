enum pState {
  reset,
  waiting,
  plotting,
  buffering,
  noconnection,
  error
};

enum pWorld {
  plotter,
  text,
  drawing
};

void setState(pState state);


