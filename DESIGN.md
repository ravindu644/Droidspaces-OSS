# DESIGN.md

The visual language of the Droidspaces Android app, Material 3 Expressive edition. The values
here come from three places and nowhere else: the agreed design boards, the Material 3
component tokens shipped in `androidx.compose.material3:material3:1.5.0-alpha18`, and the
parts of today's app that the redesign keeps on purpose. Where the app still disagrees with
this file, the app is mid-conversion. `ROADMAP-MD3E.md` says what has landed.

The C backend has no UI, so none of this applies to `src/`.

## How to use this file

**Components come first, values second.** The Reuse Inventory in
[CONTRIBUTING.md](./CONTRIBUTING.md) lists what already exists. Go there first. A dialog is
`DsDialog`, a dialog footer is `DialogFooterRow`, an empty list is `EmptyState`, a setup
screen is built from `SetupHero.kt`. If one of those fits, use it and stop reading here.

**Material 3 components are the default.** `Button`, `ListItem`, `Switch`, `Slider`,
`FilterChip`, `ModalBottomSheet`, `DropdownMenu`, `ButtonGroup`, `ToggleButton`,
`ShortNavigationBar`, `LoadingIndicator`. A hand-built `Surface` with its own radius, fill
and border is what the old app did, and it has to be argued for now. The component carries
its own shapes, colours, states, motion and accessibility, which is the point of using it.

Never hardcode a hex colour, an sp size, or a dp you picked by eye. Every number below maps
to a theme token or a component default, and the mapping is written next to it.

## What the app looks like, and why

**Tonal, not bordered.** Depth is a surface step, never a 1dp outline. A card is
`surfaceContainer` on `background`. A box inside a card is `surfaceContainerHigh`. There are
no card borders, no row borders, no divider under every header. The only lines left are the
one divider inside a card that separates its header from its body, and the AMOLED row
separator described under Colour. Shadows exist only where a component draws them itself:
the FAB and its menu, an anchored menu, a bottom sheet.

**Groups, not cards.** Rows that belong together (device info, settings, form switches,
users) sit in a segmented group: rows of `surfaceContainer` with a 2dp gap of `background`
between them, 24dp corners on the outer edges and 6dp on the inner ones. The group reads as
one object with visible joints. Carding every row is the old look. `SegmentedGroup` in
`ui/component/SegmentedGroup.kt` hands each row its shape; it does not use
`ListItemDefaults.segmentedShapes`, whose outer corners are the theme's 16.

**One highlighted surface per screen.** `primaryContainer` with `onPrimaryContainer` is
reserved for the one thing a screen wants you to see first: the backend status card on Home,
the hero of a setup screen. Nothing else on that screen uses it.

**State is icon, text and accent, never colour alone.** A running container shows a
tinted icon tile, a "Running" chip with a dot, and a PID line. Remove the colour and it still
reads. The accent roles below carry the state; fills stay quiet.

**Title case everywhere.** No uppercase labels, no tracked-out caps, no "BACKEND: WORKING".
Expressive typography gets its emphasis from weight and size, not from capitals.

## Colour

Roles come from `MaterialTheme.colorScheme`. The scheme is built in `ui/theme/Theme.kt`,
which stays the only file allowed to name a colour literally. The theme entry point is
`MaterialExpressiveTheme` with `MotionScheme.expressive()`; the palette builders behind it
are unchanged.

| Surface | Role |
| --- | --- |
| Screen background | `background` |
| Card, list row, segmented group row | `surfaceContainer` |
| Box inside a card (stats, info row), pressed or selected card | `surfaceContainerHigh` |
| Option chip, stopped-status chip | `surfaceContainerHighest` |
| Bottom sheet | `surfaceContainerLow` |
| Console (logs, terminal) | `surfaceContainerLowest` |
| Navigation bar | `surfaceContainer` |
| Anchored menu | `surfaceContainerHigh` |

| Text | Role |
| --- | --- |
| Headlines, titles, values | `onSurface` |
| Supporting text, metadata, labels in a box | `onSurfaceVariant` |
| Section label | `primary` |
| Text on the highlighted card | `onPrimaryContainer` |
| Disabled | the component's own disabled colours |

Text never gets a `.copy(alpha = ...)`. Secondary text is `onSurfaceVariant`, full strength.
The old 0.6 and 0.7 fades are gone; the role already carries the contrast step.

### What the colours mean

| State or action | Accent |
| --- | --- |
| Running, healthy, Start, the primary action of a card | `primary` on `onPrimary` (filled) |
| Running chip, running icon tile | `primaryContainer` on `onPrimaryContainer` |
| Stop, destructive, Uninstall | `error`; a filled Stop segment is `errorContainer` on `onErrorContainer` |
| Done with a caveat | `warningContainer`, the extended colour in `ui/theme/Color.kt` |
| In progress, restarting, downloading | `tertiary` |
| Promotional, "new here?" | `tertiaryContainer` on `onTertiaryContainer` |
| Tonal action segment (Restart, Logs), toggle-group unselected, nav indicator | `secondaryContainer` on `onSecondaryContainer` |
| Stopped, idle, nothing to report | `surfaceContainerHighest` on `onSurfaceVariant` |
| Backend attention | `errorContainer`; update available `tertiaryContainer` |

The init system screens keep their hand picked traffic light in `statusColorFor()` in
`ui/screen/InitServiceScreen.kt`, for the reason in Decided exceptions.

### Dynamic colour, palettes, dark and AMOLED

The scheme is chosen in this order in `Theme.kt`: AMOLED over dynamic dark, dynamic, AMOLED
over a static palette, static dark, static light. Dynamic colour is on by default on Android
12 and later. Six static palettes (Catppuccin, Ocean, Forest, Sunset, Amethyst, Sakura) cover
older devices and users who turn dynamic colour off; the accent picker in Settings only
appears in that case.

AMOLED blacks out `background`, `surface`, `surfaceVariant` and the five
`surfaceContainer` roles. Three things must survive it, and the theme is written so they do:

- The accent containers stay tonal. Under dynamic colour `primaryContainer`,
  `secondaryContainer` and `tertiaryContainer` blend toward black at 0.7; under a static
  palette `primaryContainer` stays the dark scheme's opaque value. The status card, the
  running icon tile and the chips keep their colour on black.
- Segmented groups keep their joints. On AMOLED the 2dp gap and the rows are the same
  black, so each row draws a 1px `outlineVariant` separator at the top, and that is the one
  place a line is allowed back. `outlineVariant` is already overridden to `primary` at 0.25
  in the static AMOLED scheme for this.
- Boxes inside cards stay visible. `surfaceContainerHigh` collapses to black too, so a box
  on AMOLED falls back to the same 1px `outlineVariant` edge.

## Typography

The scale lives in `ui/theme/Type.kt`. The Expressive face is Roboto Flex, and every
Emphasized slot is real (the full `Typography` constructor, not the 15-slot one that aliases
them). Use the named styles; there are no `fontSize` overrides in the design.

UI text is the Expressive scale. Anything the machine produced, a log line, a unit name, a
path, a config file, is JetBrains Mono from `Type.kt`.

| Element | Style | Size |
| --- | --- | --- |
| Top app bar title | `titleLarge` SemiBold | 22/28 |
| Card title, container name | `titleMediumEmphasized` | 18/24, weight 600 |
| List row headline | `bodyLarge` Medium | 16/22 |
| Status card wordmark | `titleLargeEmphasized` | 20/26, weight 700 |
| Count tile number | `displaySmallEmphasized` | 36/40 |
| Body copy, label and value rows in a box | `bodyMedium` | 14/20 |
| Supporting text, metadata, PID line | `bodySmall`, `onSurfaceVariant` | 13/18 |
| Section label | `labelLarge` SemiBold, `primary` | 14/20 |
| Chip label, option chip, nav label | `labelMedium` SemiBold | 12/16 |
| Button label, pill label | `labelLarge` SemiBold | 14/20 |
| Sheet title | `headlineSmall` SemiBold | 24/30 |
| Setup headline | `displaySmall` Bold | the setup flow keeps its own |
| Machine output | `bodySmall` JetBrains Mono | 12/19 |

## Spacing

The scale is **2, 4, 8, 12, 16, 20, 24**. The values are the Material 3 list, button and
chip tokens, so a component used at its defaults already lands on them.

| Gap | Value |
| --- | --- |
| Screen gutter | 16 |
| Content top, under the 64dp app bar | 8, so the first card starts at the same y on every screen |
| Between cards and groups | 12 |
| Card and box inner padding | 16, all sides |
| Label and value rows inside a box | 12 apart |
| Divider inside a card | 12 above and below |
| Section label | 20 above, 8 below, 4 side inset |
| List row | 72 tall for two lines, 56 for one, 16 side padding, 10 top and bottom |
| Leading icon to text | 12 |
| Text to trailing control | 16 |
| Segmented group | 2 between rows |
| Chip | 32 tall, 16 side padding, 8 between dot and label |
| Sheet | 48 handle band, 16 sides, 24 bottom |
| Dialog | 24 inset, as `DsDialog` already does |
| Floating action button | 16 from the right edge, 16 above the navigation bar |

### Card headers line up across tabs

The status card on Home and the container cards on Containers and Panel all put a 44dp
header row first: a 24dp icon or a 44dp icon tile, the title, and a chip on the right. Below
it, after a 12dp gap, the divider. Because every card uses 16dp padding and the same header
height, the dividers sit on one line when the user swipes between tabs, and so do the top
edges. The numbers live in `ui/component/CardMetrics.kt`; change them there or nowhere.

## Shape

Shapes read from `MaterialTheme.shapes`, which `Theme.kt` sets to the Expressive scale. The
token is named next to each use so a custom radius stands out in review.

| Element | Radius | Token |
| --- | --- | --- |
| Card, list row group outer corners, sheet rows | 24 | `extraLarge` |
| Bottom sheet, top corners | 28 | `extraLarge` of the sheet component |
| Box inside a card, anchored menu, highlighted info row | 16 | `large` |
| Detail pill, text field, FAB | 16 | `large` |
| Segmented group inner corners, button group inner corners | 6 and 8 | `extraSmall` |
| Status chip, option chip | 8 | `small` |
| Button group outer corners, toggle button | full | `CircleShape` |
| Setup hero | `MaterialShapes.Cookie12Sided` | the setup flow only |
| Icon tile on a card | 14 | `medium` |

## Components

Each of these exists in material3 1.5.0-alpha18. Opt in per file with
`@OptIn(ExperimentalMaterial3ExpressiveApi::class)`, never with a module-wide flag.

**Top app bar.** The small `TopAppBar` at 64dp everywhere. On the three tabs it shows the
Droidspaces bars glyph, 24dp, then the tab title; on sub-screens the back arrow then the
title. Actions are 48dp icon buttons on the right: settings on the tabs, refresh on
container details, plus on the terminal. A screen whose primary action is a commit (Edit
container) puts a filled `Button` labelled Save in the actions slot, the way Contacts does.
No large or medium app bars: they left an empty band under the title.

**Navigation bar.** `ShortNavigationBar` with three items. The active item gets the
`secondaryContainer` pill behind its icon and a SemiBold label. The bar is `surfaceContainer`
with no top divider. Tabs switch with `animateScrollToPage`.

**Button group.** The container card's actions are a connected `ButtonGroup`: the state
action (Start or Stop) is the wide filled segment, Restart and Logs are 56dp
`secondaryContainer` segments, 4dp apart, outer corners full and inner corners 8. Network
mode in the edit form is the same group as a single-select set. A `ToggleButton` inside the
group morphs from 8dp to full corners when checked.

**Toggle button.** Shell user on container details, Ctrl and Alt in the terminal keys. A
checked toggle is `primary` on `onPrimary`, unchecked is `secondaryContainer`.

**Chips.** The status chip is a 32dp `surfaceContainerHighest` chip with a 6dp dot and a
label; running uses `primaryContainer`. Option chips (Sandboxing, Run at boot) are the same
chip without a dot. Chips are labels here, never buttons.

**Anchored menu.** Per-item secondary actions (Edit configuration, Migrate or Resize sparse
image, Export, Uninstall) open an anchored `DropdownMenu` on long press, or on tap for a
stopped card, which is what the old drawer did. Items are 48dp with a 24dp leading icon and a
full label, the destructive item after a divider in `error`. The pressed card gets a 2px
`primary` outline so the anchor is obvious. On expanded windows the same menu is the right
answer, which is why this is a menu and not a sheet.

**Floating action button.** One per screen, bottom right. More than one action behind it is
a `FloatingActionButtonMenu` with a `ToggleFloatingActionButton`, as the Containers tab
already does. The vibrant `HorizontalFloatingToolbar` is reserved for a future multi-select
mode and is not used yet.

**Bottom sheets.** Logs, the new terminal session picker and Privileged mode are
`ModalBottomSheet`s: `surfaceContainerLow`, 28dp top corners, the 32x4 drag handle in a 48dp
band, title row with a 48dp close or refresh button, content, then actions within thumb
reach. The sheet caps at 640dp wide; on an expanded window a sheet that only lists actions
becomes a menu.

**Lists.** Settings, the edit form, device info and users are `ListItem`s in a segmented
group. A switch row is a two-line item with a trailing `Switch`. A row that opens something
has a trailing chevron. A row that holds a value shows it in `onSurfaceVariant` before the
chevron.

**Progress.** `LoadingIndicator` for indeterminate waits, through the `LoadingSize` wrapper in
`ui/util/LoadingIndicator.kt`. Live CPU, memory and disk are plain 4dp
`LinearProgressIndicator`s with the stop dot. No wavy indicators on stats: they were tried and
rejected as noise. The download bar in the rootfs sheet keeps its wavy indicator because a
download is a single progressing event.

**Tabs.** Terminal sessions are primary tabs under the app bar row, with a close icon in the
tab and a 3dp indicator.

## The detail pill

Copy login, Open and Manage on container details are one shape, kept from today's app
because it already fit: an M3 `Button`, 40dp tall, `widthIn(min = 140.dp)`,
`shapes.large` (16), `primary` on `onPrimary`, the default 24dp content padding,
`labelLarge` SemiBold, a 16dp chevron (or an 18dp copy icon) and a 6dp gap. It trails its
row; Copy login sits right aligned on its own row under the user toggles. The Copy
button on the requirements screen is the same pill. Four sites, one composable,
`ui/component/DetailPill.kt`.

## Dialogs

`DsDialog` stays the shell for confirmations, typed-phrase guards and anything with a
cancel and a confirm. Its layout rules are unchanged: `Surface` with 24dp padding bounded by
`LocalConfiguration`, a weighted scrolling body, the footer measured first. The title is
`titleLarge` SemiBold, left aligned. Pickers and long option lists are sheets, not dialogs.

Every dialog ends with `DialogFooterRow`: two equal-weight 48dp buttons, radius 16, a quiet
tonal dismiss and a filled confirm, destructive confirms in `error`. Labels are one line.

## Icons and touch targets

| Use | Size |
| --- | --- |
| List row leading icon, app bar icon, menu item icon | 24 |
| Icon inside a button or button-group segment | 20 |
| Chip icon, copy icon in the detail pill | 18 |
| Chevron in the detail pill, metadata icon | 16 |
| Icon tile on a container card | 44 tile, 24 glyph |
| Empty state hero | 64 |
| Chip dot | 6 |

Icons have one meaning each: `receipt_long` is logs, `terminal` is a shell, `check_circle`,
`error` and `update` are the backend state, `verified` is the badge on the Home status card
and nothing else.

Anything tappable is at least **48dp**. That is an accessibility floor, not a style
preference, and the one rule in this file a design argument does not get to override.

## Alignment

Rows are `Alignment.CenterVertically`. Card and dialog titles are left aligned. A header row
that pairs a title with a chip or a control uses `Arrangement.SpaceBetween`. Centred text is
reserved for full screen states: empty lists, loading, setup heroes.

## Motion and haptics

Animation specs come from `MaterialTheme.motionScheme`, which the theme sets to
`MotionScheme.expressive()`: `defaultSpatialSpec()` for size and position,
`defaultEffectsSpec()` and `fastEffectsSpec()` for fades and colour, `slowEffectsSpec()` for
a result settling in. Never hand-type a spring or a tween.

Screen transitions are fades, except Home entering from the installer, which scales in from
0.9. The container card to container details transition is a shared-element candidate once
navigation moves to 2.9, which also brings predictive back. The setup hero morphs between
`MaterialShapes` presets through `ui/component/MorphShape.kt`.

Haptics go through `rememberHaptics()` in `ui/component/Haptics.kt`, one method per row below,
and fire where the platform does:

| Moment | Type |
| --- | --- |
| Start or stop completed, install completed | `Confirm` |
| Operation failed | `Reject` |
| Switch or toggle button flipped | `ToggleOn`, `ToggleOff` |
| Slider stepped | `SegmentTick` |
| Manage menu opened by long press | `LongPress` |
| Pull to refresh passed its threshold | `GestureThresholdActivate` |

Loading is `LoadingIndicator` with a `LoadingSize`, whole screen loading is
`FullScreenLoading`, and `LoadingSize.Hero` is the setup-flow hero loader.

### The setup flow

Welcome, the feature pager, the root check and the backend installer share one geometry, and
it lives in `ui/component/SetupHero.kt`. `SetupPage` is the skeleton under the Scaffold: a 40dp
row above, the frame slot that centres its content or scrolls when the window is too short, and
a 44dp row below. `SetupFrame` is the frame: a 240dp hero box, then the title and the copy in a
fixed text block. `SetupHero` is the hero of a screen that does work, a 240dp
`MaterialShapes.Cookie12Sided` in `primaryContainer`, `warningContainer` or `errorContainer`
holding the loader or a 96dp glyph, and only its colour and content change between states.
`SetupActionBar` is the one button: a full pill with no bar surface and no divider. Build a
new setup screen out of these and the hero, the title and the button land on the same rows
as every other.

## Adaptive

The app is designed for compact windows. Tablet and foldable layouts (list-detail for
containers, a navigation rail) are a roadmap item, not a current rule. Until then, sheets
cap at 640dp and nothing else stretches.

## When the rule does not fit

Sometimes it will not, and the answer is not to quietly use a different number.

Say it in the PR, and leave a comment at the site explaining what the deviation buys. An
undocumented deviation is drift and the next contributor will "fix" it. A documented one is a
decision, and it survives.

## Decided exceptions

Looked at, deliberately left alone. Do not re-open these without a reason the original one
misses.

- **The FAB menu is elevated.** `ToggleFloatingActionButton` and `FloatingActionButtonMenuItem`
  draw the Material shadow and take no elevation parameter. A button floating over a scrolling
  list is the one place a shadow does structural work.
- **`TerminalDialog` and `ProgressDialog` do not use `DsDialog`.** The first becomes the logs
  sheet in phase 4; until then it keeps its own header row. The second is not dismissible.
- **The init system status hues** (`statusColorFor()` in `ui/screen/InitServiceScreen.kt`)
  stay a hand picked traffic light rather than theme roles. Six of the seven states can sit in
  the legend row at once, and `primary` and `tertiary` move with dynamic colour, so running
  and abnormal would collapse onto nearly the same colour under some palettes.
- **The terminal virtual keys background** stays a literal. It sits against the terminal's
  own black, which does not follow the app theme.
- **The setup flow's button** (`SetupActionBar`) is a full pill with no bar surface and no
  divider. Its disabled colours stay equal to its enabled colours: it is disabled only while
  a check or an install is in flight, and greying it for that moment reads as a flicker.
- **The unit detail and override editor titles** keep a smaller style than every other
  screen title. Both display systemd unit names, which are long.
- **Live stats use plain bars, not wavy ones.** CPU, memory and disk on Panel and container
  details are `LinearProgressIndicator`s. The wave reads as noise on a number that idles at
  zero. The rootfs download keeps its wavy bar.
- **The Home status card is the only `primaryContainer` surface on the tabs.** Count tiles,
  device rows and container cards stay `surfaceContainer` so the card is the one thing that
  draws the eye.
- **The AMOLED row separator** is the one line allowed on a segmented group. Without it the
  rows merge into one black slab.
- **`EmptyState` is not adopted** by `ui/screen/AutoBootPriorityScreen.kt` or the private
  empty state in `ui/screen/InitServiceScreen.kt`: absorbing them needs roughly seven new
  parameters for two call sites.
- **Faint fill alphas of 0.03, 0.04 and 0.06** in the terminal dialog, the terminal screen and
  the sparse image screen stay unshared until those screens convert.
