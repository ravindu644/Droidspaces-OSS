package com.droidspaces.app.ui.terminal.virtualkeys;

import java.util.Arrays;
import java.util.HashMap;
import java.util.List;

public class VirtualKeysConstants {

  /** Aliases for the keys */
  public static final VirtualKeyDisplayMap CONTROL_CHARS_ALIASES =
      new VirtualKeyDisplayMap() {
        {
          put("ESCAPE", "ESC");
          put("CONTROL", "CTRL");
          put("SHFT", "SHIFT");
          put("RETURN", "ENTER"); // Technically different keys, but most applications won't see
          // the
          // difference
          put("FUNCTION", "FN");
          // no alias for ALT

          // Directions are sometimes written as first and last letter for brevety
          put("LT", "LEFT");
          put("RT", "RIGHT");
          put("DN", "DOWN");
          // put("UP", "UP"); well, "UP" is already two letters

          put("PAGEUP", "PGUP");
          put("PAGE_UP", "PGUP");
          put("PAGE UP", "PGUP");
          put("PAGE-UP", "PGUP");

          // no alias for HOME
          // no alias for END

          put("PAGEDOWN", "PGDN");
          put("PAGE_DOWN", "PGDN");
          put("PAGE-DOWN", "PGDN");

          put("DELETE", "DEL");
          put("BACKSPACE", "BKSP");

          // easier for writing in termux.properties
          put("BACKSLASH", "\\");
          put("QUOTE", "\"");
          put("APOSTROPHE", "'");
        }
      };
  /**
   * Defines the repetitive keys that can be passed to {@link
   * VirtualKeysView#setRepetitiveKeys(List)}.
   */
  public static List<String> PRIMARY_REPETITIVE_KEYS =
      Arrays.asList("UP", "DOWN", "LEFT", "RIGHT", "BKSP", "DEL");
  public static class VirtualKeyDisplayMap extends HashMap<String, String> {}

  /*
   * Multiple maps are available to quickly change
   * the style of the keys.
   */

  public static class EXTRA_KEY_DISPLAY_MAPS {
    /** Keys are displayed in a natural looking way, like "→" for "RIGHT" */
    public static final VirtualKeyDisplayMap CLASSIC_ARROWS_DISPLAY =
        new VirtualKeyDisplayMap() {
          {
            // classic arrow keys (for ◀ ▶ ▲ ▼ @see arrowVariationDisplay)
            put("LEFT", "←"); // U+2190 ← LEFTWARDS ARROW
            put("RIGHT", "→"); // U+2192 → RIGHTWARDS ARROW
            put("UP", "↑"); // U+2191 ↑ UPWARDS ARROW
            put("DOWN", "↓"); // U+2193 ↓ DOWNWARDS ARROW
          }
        };

    public static final VirtualKeyDisplayMap WELL_KNOWN_CHARACTERS_DISPLAY =
        new VirtualKeyDisplayMap() {
          {
            // well known characters // https://en.wikipedia.org/wiki/{Enter_key,
            // Tab_key,
            // Delete_key}
            put("ENTER", "↲"); // U+21B2 ↲ DOWNWARDS ARROW WITH TIP LEFTWARDS
            put("TAB", "↹"); // U+21B9 ↹ LEFTWARDS ARROW TO BAR OVER RIGHTWARDS ARROW TO
            // BAR
            put("BKSP", "⌫"); // U+232B ⌫ ERASE TO THE LEFT sometimes seen and easy to
            // understand
            put("DEL", "⌦"); // U+2326 ⌦ ERASE TO THE RIGHT not well known but easy to
            // understand
            put("DRAWER", "☰"); // U+2630 ☰ TRIGRAM FOR HEAVEN not well known but easy to
            // understand
            put("KEYBOARD", "⌨"); // U+2328 ⌨ KEYBOARD not well known but easy to understand
            put("PASTE", "⎘"); // U+2398
          }
        };

    public static final VirtualKeyDisplayMap NICER_LOOKING_DISPLAY =
        new VirtualKeyDisplayMap() {
          {
            // nicer looking for most cases
            put("-", "―"); // U+2015 ― HORIZONTAL BAR
          }
        };

    /** Some classic symbols everybody knows */
    public static final VirtualKeyDisplayMap DEFAULT_CHAR_DISPLAY =
        new VirtualKeyDisplayMap() {
          {
            putAll(CLASSIC_ARROWS_DISPLAY);
            putAll(WELL_KNOWN_CHARACTERS_DISPLAY);
            putAll(NICER_LOOKING_DISPLAY);
            // all other characters are displayed as themselves
          }
        };
  }

}
