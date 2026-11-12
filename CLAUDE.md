# Working on Baaz

Baaz is an Archicad 30 add-on (Windows, C++20, Win32 + GDI+ + the Archicad C++ API) that
opens a keyboard command palette on a Space tap. It runs inside Archicad's UI thread and hooks
every keystroke and click there, and the projects it runs on are often under client
confidentiality. A crash takes Archicad and the user's unsaved work down with it.
Correctness, safety and privacy come before features.

Every change must follow the rules in [REVIEW.md](REVIEW.md). They are imported here:

@REVIEW.md

## Layout

| Path | What lives there |
| :--- | :--- |
| `addon/src/BaazAddon.cpp` | Archicad entry points (`CheckEnvironment`, `Initialize`, `FreeData`). |
| `addon/src/MessageHook.cpp` | Thread hooks: Space tap vs hold, Magic Wand replay, keys while the HUD is open. |
| `addon/src/HudWindow.cpp` | The layered, focusless search window and its key handling. |
| `addon/src/SearchEngine.cpp` | Tool table, chords, ranking and the `?` cheatsheet. |
| `addon/src/NavigatorIndexer.cpp` | Views, layouts and layer combinations of the open project. |
| `addon/src/CommandExecutor.cpp` | Runs a result: tool, view, layout, layer combination, settings. |
| `addon/src/SchemeReader.cpp` | Reads an exported command-layout scheme (XML) into a menu tree; no Archicad API. |
| `addon/src/BinSchemeReader.cpp` | Reads the binary command scheme Archicad keeps current (`.bin`) into a menu tree; no Archicad API. |
| `addon/src/MenuLink.cpp` | Links the names of a menu tree to the positions in the live menu bar; no Archicad API. |
| `addon/src/CommandSource.cpp` | Win32 menu bar (`LiveMenu`), finds the scheme `.bin` files, the profile in use and the exports, picks the scheme and the names, runs a command by `WM_COMMAND`; no Archicad API. |
| `addon/src/Settings.cpp`, `SettingsDialog.cpp` | `%USERPROFILE%\.baaz\config.json` and its dialog. |
| `addon/resources/` | `RINT/AddOn.grc` (name), `RFIX/AddOnFix.grc` (MDID), `RFIX.win/AddOnMain.rc2`. |
| `tests/` | Native tests, built without the Archicad API. `test_sendinput_stub.hpp` records synthesized keys. |
| `tools/check_rules.py` | The REVIEW.md checks a compiler can't do. |
| `tools/CompileResources.py` | Tapir's resource compiler, run by the build (R22). |

## Commands

Run these before every commit; CI runs the same:

```bash
python tools/check_rules.py
python -m lizard addon/src tests -C 12 -m -w -W whitelizard.txt
cmake -S . -B build -DARCHICAD_DEVKIT_DIR="<DevKit folder>" -DBAAZ_WARNINGS_AS_ERRORS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

`lizard` is a development tool only (`pip install lizard==1.17.13`). Without a DevKit the
configure step builds only the tests; CI builds the add-on too.

## How to work

1. **Understand before changing.** Read the file you are changing, its tests, and the code
   that calls it. Search for an existing helper before writing one.
2. **Agree on the plan for anything non-trivial.** A new key or chord behaviour, a new
   setting, a new dependency, or anything that changes the project model or touches files
   needs the maintainer's go-ahead first. Say what you intend and why, then wait.
3. **Make the smallest change that does the job.** No unrequested features, options,
   refactors or "improvements" alongside it.
4. **Test it.** Write the test that proves the change, see it fail without the change,
   then run the full checks above. Nothing is "done" while a check is red.
5. **Re-read your own diff** as a reviewer would, against REVIEW.md, and fix what you find.
6. **Report honestly.** Say what you verified, what you could not (there is no Archicad in
   CI, so behaviour inside Archicad is unverified until the maintainer tests it), and any
   shortcut you took.

## Never

- Merge a pull request, push to `main`, rewrite published history or force-push.
- Weaken or delete a test, or skip a check, to get green.
- Commit generated resources, build output, a borrowed MDID or `tools/check_mdid.py`.
- Add a dependency, a network call, or anything that changes the user's project, without the
  maintainer's explicit agreement.
- Put secrets, tokens, real project data or personal data in code, tests, logs or commits.
