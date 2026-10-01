# native — the Pokémon companion screen on a Mac

Compiles the firmware's companion views (`src/pokemon_state.cpp`, `src/gbgfx.cpp`,
`src/pokemon_views.cpp`, `src/pack.cpp`) unmodified against small stand-ins for the
Arduino core, LittleFS and Adafruit_GFX, so the 680×920 e-paper frame can be rendered
and looked at without hardware. The stub games server (`tools/www_stub_server.py`)
plays the ROM in a browser; `eink_server.py` polls its work-RAM mirror and shows the
frame on a page that refreshes once a second.

    ./build.sh                 # -> ./pokeview
    ./start-stub.sh &          # http://localhost:8080/  play Pokémon Red here
    ./start-eink.sh &          # http://localhost:8081/  the companion screen

`GAMES_ROOT` (default `~/Projects/wifi-ereader/stub_games`) holds `roms/`, `saves/` and
`aux/pokered.pack`, outside the repo. Pad buttons follow `src/games.cpp`: UP next page,
DOWN previous page, LEFT terrain on/off; a battle takes the screen by itself.

    ./pokeview --root $GAMES_ROOT --wram out/x.wram --decode          # state as JSON
    ./pokeview --root $GAMES_ROOT --wram out/x.wram --all out/x       # three PGM frames
