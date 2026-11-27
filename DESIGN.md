# DESIGN.md

The visual language of the Droidspaces Android app. Every value here was read out of
`Android/app/src/main/java/com/droidspaces/app/`, not invented for this document. Where the app
disagreed with itself, the value most of the app already uses won.

The C backend has no UI, so none of this applies to `src/`.

## How to use this file

**Components come first, values second.** The Reuse Inventory in
[CONTRIBUTING.md](./CONTRIBUTING.md) lists what already exists. Go there first. A card is
`SettingsCard`, a dialog footer is `DialogFooterRow`, a status chip is `StatusPill`, an empty
list is `EmptyState`. If one of those fits, use it and stop reading here.

This file is for the case where you have checked and nothing shared covers what you need. Then
you build it out of the values below, so the new thing looks like it belongs.

Never hardcode a hex colour, an sp size, or a radius you picked by eye.

## What the app looks like, and why

**Flat, always.** Nothing in this app is elevated. `tonalElevation = 0.dp` appears at 55 sites
and there is not one `CardDefaults.cardElevation` in the tree. Depth is expressed by stepping
the surface colour up one level and drawing a 1dp border. If you reach for a shadow, you have
left the design. The one thing that floats is the FAB menu on the Containers tab, and that is a
decided exception below, not a precedent.

**`Surface` is the primitive.** 123 `Surface` uses against a single `Card()`. Buttons are
clickable Surfaces, not Material `Button`s. Dialogs are `Dialog { Surface { } }`, not
`AlertDialog`. Material components are not banned, but they arrive with their own elevation and
radius defaults and have to be argued for.

**Colour is derived, never literal.** `ui/theme/Theme.kt` blends a full scheme from three
palette colours. Dynamic colour is on by default, six fallback palettes ship for older devices
and for users who turn it off, and AMOLED collapses every surface to black. A hardcoded
`Color(0xFF...)` breaks all four paths at once.

**Borders and tint carry state. Fills stay quiet.** A running container is not a green card. It
is a normal card with a tinted button and an accent border. State lives in the accent, not in
the background.

## Colour

Roles come from `MaterialTheme.colorScheme`. The scheme itself is built in `ui/theme/Theme.kt`,
which is the only file allowed to name a colour literally.

| Surface | Role |
| --- | --- |
| Screen background | `background` |
| Card sitting on the background | `surfaceContainer` |
| Card or pill nested inside another surface, and interactive settings rows | `surfaceContainerHigh` |
| Dialog and bottom sheet shell | `surfaceContainer` |

| Text | Role |
| --- | --- |
| Primary | `onSurface` |
| Secondary, supporting, metadata | `onSurfaceVariant` at alpha `0.7f` |
| Tertiary, deliberately quiet | `onSurfaceVariant` at alpha `0.6f` |
| Disabled content | `onSurface` at alpha `0.38f` |
| Disabled container fill | `onSurface` at alpha `0.12f` |

Borders are always 1dp. The alpha is what changes, and it is the value that has drifted the most
in this app, so it is worth getting right:

| Border | Colour |
| --- | --- |
| Card | `outlineVariant` at `0.35f` |
| Dialog shell | `outlineVariant` at `0.4f` |
| Action pill wrapper and other nested surfaces | `outlineVariant` at `0.2f` |
| Divider (`HorizontalDivider`) | `outlineVariant` at `0.3f` |
| Text field, unfocused | `outlineVariant` at `0.5f` |
| Text field, focused | `primary` at `0.8f` |
| Tinted button, matching its accent | the accent role at `0.2f` |

### What the colours mean

The container lifecycle is consistent across the app and new states should join it rather than
invent a parallel vocabulary.

| State | Accent |
| --- | --- |
| Running, healthy, start action | `primary` |
| Stopped, failed, destructive, stop action | `error` |
| Done with a caveat, warning | `warningContainer`, an extended colour in `ui/theme/Color.kt` |
| Restart action | `secondary` |
| In progress, restarting, downloading | `tertiary` |
| Idle, unknown, nothing to report | `onSurfaceVariant` at `0.6f` |
| Disabled | fill `onSurface` at `0.08f`, accent `outlineVariant` |

A tinted button takes its fill from the matching container role at alpha `0.4f`, for example
`primaryContainer.copy(alpha = 0.4f)` for start and `errorContainer.copy(alpha = 0.4f)` for stop,
with a 1dp border of the accent at `0.2f`.

The init system screens are the one documented exception. Their status hues live in
`statusColorFor()` in `ui/screen/InitServiceScreen.kt` and are a hand picked traffic light, not
theme roles, because six of those states can sit in the legend row at once and `primary` and
`tertiary` move with dynamic colour and the palette picker. Take a status colour from that
function, never from a literal.

## Typography

The scale lives in `ui/theme/Type.kt` and all fifteen slots are configured. Use the named styles.
There are only twelve `fontSize` overrides in the whole app and none of them are a good example.

UI text is `FontFamily.Default`. Anything the machine produced, a log line, a unit name, a config
file, a path, is JetBrains Mono, imported from `ui/theme/Type.kt`. Do not declare a local font
family.

| Element | Style |
| --- | --- |
| Screen title in a `TopAppBar` | `titleLarge`, Bold |
| Dialog title | `titleLarge`, Bold, left aligned |
| Card title | `titleMedium`, SemiBold |
| Section header inside a screen | `titleSmall`, Bold, `primary` |
| Button label | `labelLarge`, SemiBold |
| Body copy in a dialog | `bodyMedium` |
| Helper text, metadata, subtitles | `bodySmall` with `onSurfaceVariant` at `0.7f` |
| Status pill label | `labelSmall`, Black, `letterSpacing = 0.5.sp`, uppercase |
| Machine output | `bodySmall` with the JetBrains Mono family |

## Spacing

The scale is **4, 8, 12, 16, 20, 24, 32**. Nothing else. If a gap feels like it wants 14, it
wants 12 or 16.

| Gap | Value |
| --- | --- |
| Screen edge, lists and dashboards | 16 horizontal |
| Screen edge, wizard and setup flows | 24 horizontal |
| Screen edge, any screen below 320dp wide | 8 horizontal, via `screenGutter()` |
| Between cards in a list | 16 |
| Between blocks inside a card | 12 |
| Card inner padding | 16 |
| Dialog inner padding | 24 |
| Icon to its label, chip to chip | 8 |
| Tight metadata rows | 4 |

Screens that sit under the floating tab bar get its measured height, system nav inset included,
as `bottomBarInset`, and add `bottomBarInset + 16.dp` to their content padding so the last card
clears it. Never a fixed dp: three-button nav's 48dp inset makes the bar much taller than gesture nav does.

### Card headers line up across tabs

The status card on the home tab, the container card, the running container card and the init
service row all put a title on the left and a status pill on the right, above a divider. A user
switching tabs sees those dividers and pills hold the same line, and that is deliberate.

It only holds while all four use the same numbers, so they come from `CardContentPadding` and
`CardHeaderHeight` in `ui/component/CardMetrics.kt` rather than being typed out per card. The
header is 48dp because the container card puts an icon button in it.

If you add a card with this shape, take the values from there. If you change them, you are
moving every card header in the app, which is the intended way round.

## Shape

| Element | Radius |
| --- | --- |
| Card | 20 |
| Dialog shell | 24 |
| Bottom sheet, top corners only | 24 |
| Full width primary button | 20 |
| Dropdown menu | 20 |
| Action button | 16 |
| Text field | 16 |
| Action pill wrapper | 12 |
| Status pill, small badge | 8 |
| FAB and FAB menu items | component default, the toggle morphs its own radius |

## The action pill

This is the pattern that keeps getting "fixed", so it gets its own section.

A row of related actions sits inside a tinted wrapper:

```kotlin
Surface(
    shape = RoundedCornerShape(12.dp),                    // wrapper
    color = MaterialTheme.colorScheme.surfaceContainerHigh,
    border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.2f))
) {
    Row(Modifier.padding(4.dp), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
        Surface(
            modifier = Modifier.weight(1f).height(48.dp),
            shape = RoundedCornerShape(16.dp),            // button, larger than the wrapper
            color = accentContainer.copy(alpha = 0.4f),
            border = BorderStroke(1.dp, accent.copy(alpha = 0.2f))
        ) { /* icon + labelLarge SemiBold */ }
    }
}
```

**The buttons are rounder than the box they sit in. That is the design.** The 4dp inset plus the
larger inner radius makes the buttons read as sitting proud of the wrapper rather than nesting
flush inside it. Matching the two radii flattens the effect and has been reverted twice already.

Two sites use it: `ui/component/ContainerCard.kt` and `ui/screen/InitServiceScreen.kt`. It is for
a row of peer actions inside a card. A dialog's confirm and cancel are not that, and have their
own rule below.

## Floating action button

A screen has at most one FAB. It sits bottom right in the screen's root `Box`, with
`padding(end = 24.dp, bottom = bottomBarInset + 16.dp)` so it clears the floating tab bar.
Snackbars on the tab screens sit at the same `bottomBarInset + 16.dp` and may cover the FAB while
they show.

More than one action behind a FAB is a Material 3 Expressive `FloatingActionButtonMenu` with a
`ToggleFloatingActionButton` as its button, never a small FAB stacked on an extended one. The
toggle shows a plus that morphs into a cross; the items rise above it, each with an icon and a
short label, the most used one nearest the FAB. Two to six items. Colour, shape, size, icon
animation and motion stay at the component defaults, which is the point of using the component.

The expanded flag is `rememberSaveable` so it survives rotation. It closes on the toggle, on
back press (`BackHandler`), on picking an item, and on a tap on the list behind it. The toggle
carries a `contentDescription` and a `stateDescription`, and the last item carries a "Close
menu" custom accessibility action because TalkBack reaches the toggle before the first item.

One site uses it: `ui/screen/ContainersScreen.kt`. A second screen copies that block.

## Settings screens and forms

A screen of options is the settings screen's layout, the container config screen included:

```
GroupHeader("Networking")          titleSmall Bold primary, inset to the text inside the group
GroupIntro("...")                  optional, bodyMedium onSurfaceVariant @ 0.7f
SettingsGroup {                    one surface per section, radius 24, border outlineVariant @ 0.35f
  SwitchItem / NavRow / RadioRow / EntryRow / GroupField
  GroupDivider()                   between rows, inset 16
}
AddEntryButton("Add ...")          under a list, outside the group, 12dp below it
```

Inside a page the section titles are `GroupHeader(sub = true)`, `bodyLarge` Bold in
`onSurface`, the size the form's sub-titles always had. Rows follow `SwitchItem`: 56dp for one
line, 72dp for two, 12dp vertical padding, text starting on the same line whether the row leads
with an icon or a radio. A list entry is 72dp whatever it holds.

Rows sit flat inside the group. A row never draws its own card, and a section never becomes a
stack of cards. Headings are `GroupHeader`; a sub-section is another header and group, never
a bold title in between. A warning about a setting is a `DangerousWarningCard` above its
group, the same red card as the privileged page, not a tinted note in another colour.

| Setting needs | Row |
| --- | --- |
| On or off | `SwitchItem` |
| One of a few options, each worth a line of explanation | `RadioRow` in a `selectableGroup()` group |
| One of many options | `DsDropdown` inside a `GroupField` |
| Free text | `OutlinedTextField` inside a `GroupField` |
| A list, a set of related fields, or more than fits in a row | `NavRow` to a page, showing the current value |

**Pages, sheets and dialogs.** A page is a full screen (`ConfigPageScaffold`) for an option with
its own structure: a list, several fields, a draft. A sheet (`DsBottomSheet`) is for adding one
item to a list, so the list stays in view behind it. A dialog is for a decision that blocks:
a confirmation, a typed phrase.

Every page is a `DraftPageScaffold` and edits a draft. Leaving keeps the draft, the way the
form always applied edits, unless the page's own rules fail: then back, from the arrow or the
system, asks "Discard edits?" so no page leaves broken settings behind. Privileged mode is the
one page with a button, because turning a flag on needs the typed phrase first: its "Done"
`PrimaryActionBottomBar` is grey until something changed and the phrase matches, and back
there never keeps the draft.

## Bottom sheets

`DsBottomSheet` lays out like `DsDialog`: title, a body that scrolls, and a `footer` measured
first, so the actions stay above the keyboard. It stops below the status bar, pads for the
navigation bar and the keyboard itself, and skips the half-open state. The footer is a
`DialogFooterRow`.

## Small screens

Droidspaces runs on 240x320 flip phones and on phones in landscape with the keyboard up, so
every screen is checked at both.

- Below 320dp wide (`isCompactWidth()`): side margins halve (`screenGutter()`), two fields
  side by side stack, footer buttons stack.
- Below 480dp tall (`isShortHeight()`): the top bar drops to 48dp and the action bar's padding
  shrinks.
- No fixed widths. Every text in a row takes `weight(1f)` and a `maxLines`.
- Test with `adb shell wm size 240x320` and `wm density 120`, then 160. Read the current
  `wm density` first and put it back after.

## Dialogs

Every dialog is `DsDialog`, and it owns the layout, not just the frame:

```
Surface(fillMaxWidth, padding 24, heightIn(max = screen - 48))   bounded by LocalConfiguration
  Column(padding 24, spacedBy 16)
    Column(weight(1f, fill = false), scrolls)   the body: shrinks, scrolls
    footer()                                    natural height, measured first
```

The bound comes from `LocalConfiguration`, not the window. The activity handles
`configChanges` itself, so a dialog open through a rotation keeps its stale window
constraints; the configuration is live, and it is what `TerminalDialog` has always sized
from.

**Actions go in the `footer` slot, never in the content.** That is the whole point of the
component. A `Column` measures its unweighted children in order against the space that is left,
so actions placed below a scrolling body get measured with whatever remains: fine in portrait,
a sliver or nothing in landscape. The footer is unweighted and declared second, so it is
measured first and always gets its full height.

Callers therefore do not set a width, their own padding, or their own scroll, and never pass
`wrapContentHeight()`, the shell already wraps. The shell handles the keyboard itself, so no
`imePadding()` either. It pads for the keyboard only, never the system bars: their insets drop
to zero while the window closes, and padding for them makes the dialog jump on dismiss. What is genuinely per-dialog rides on the `modifier`: for a dialog that
should stay modest, a cap on the whole dialog, `heightIn(max = ...)` or a `fillMaxHeight`
fraction, never on an inner list.
A shell-level cap is safe because a squeezed body scrolls instead of eating the footer.
Destructive dialogs pass `borderColor` to outline in `error`. A body built on a `LazyColumn`
passes `scrollableContent = false` and weights the list, `fill = false` if the dialog should
wrap when the list is short, because a lazy list cannot live inside a scrolling parent.

The title is `titleLarge` Bold, left aligned, and is the first thing in the content.

## Dialog actions

Every dialog ends with `DialogFooterRow`. It is two equal-weight buttons, a quiet tonal dismiss
and a filled confirm:

| | Value |
| --- | --- |
| Row | `fillMaxWidth`, `spacedBy(12.dp)` |
| Both buttons | `weight(1f).height(48.dp)` |
| Radius | 16 |
| Dismiss | fill `onSurface @ 0.06f`, border `1.dp outlineVariant @ 0.35f`, label `onSurfaceVariant` |
| Confirm | fill `primary`, label `onPrimary`, no border |
| Confirm, destructive | fill `error`, label `onError`, via `destructive = true` |
| Confirm, disabled | fill `onSurface @ 0.12f`, label `onSurface @ 0.38f` |

Equal weight and a fixed height are the point: the two buttons are always the same size, and
neither one changes because of the other's label. Below 320dp wide the row stacks, confirm on
top, because two buttons side by side would leave each about 60dp.

**Labels are one line and ellipsize.** If a label wraps, the label is too long. Shorten the
string, do not grow the button. "Allow" and "Not now" beat "Grant Permission" and "I Understand"
on a control that is half a dialog wide.

A dialog with a single action uses the dismiss button on its own, full width. The footer always
draws a pair.

## Icons and touch targets

| Use | Size |
| --- | --- |
| Leading icon on a card or list row | 20 |
| Toolbar action, icon inside a button | 18 |
| Inline with `bodySmall` text | 16 |
| Dialog header, distro identity | 24 |
| Empty state hero | 64 |
| Status dot | 6 |

Anything tappable is at least **48dp**. That is an accessibility floor, not a style preference,
and it is the one rule in this file that a design argument does not get to override.

## Alignment

Rows are `Alignment.CenterVertically`. The app does this 101 times and deviates once.

Card titles and dialog titles are left aligned, always. A card row that pairs a title with a
control uses `Arrangement.SpaceBetween`.

Centred text is reserved for full screen states: empty lists, loading, setup heroes. If content
is centred inside a card, it is probably wrong.

## Motion and loading

Spinners come from `LoadingIndicator` with a `LoadingSize`, never a raw `.size(n.dp)` on a
progress indicator. Whole screen loading is `FullScreenLoading`. `LoadingSize.Hero` (144) is
the one setup-flow hero loader; the empty-state icon stays 64.
Animation timings come from `AnimationUtils`.
Hero state changes use `MaterialTheme.motionScheme`, which the theme sets to
`MotionScheme.expressive()`: `defaultSpatialSpec()` for size and position, `defaultEffectsSpec()`
for fades, `slowEffectsSpec()` for a result line fading in. Never hand-type a spring, and never
`DampingRatioMediumBouncy` on a result.

A refresh button that reloads a list in place, in a card header or a sheet title, is
`DsRefreshButton`. It spins a full turn on every tap and keeps turning while the reload runs,
stopping only at the top of a turn. Most reloads finish in milliseconds, and without the spin the
tap looks ignored. A top bar refresh that swaps the screen to its own loading state does not need
it, the loading state is the feedback.

### The setup flow

Welcome, the feature pager, the root check and the backend installer share one geometry, and
it lives in `ui/component/SetupHero.kt`. `SetupPage` is the skeleton under the Scaffold: a 40dp
row above, the frame slot that centres its content or scrolls when the window is too short, and
a 44dp row below. `SetupFrame` is the frame: a 240dp hero box, then the title and the copy in a
fixed text block. `SetupHero` is the hero of a screen that does work, a 240dp
`MaterialShapes.Cookie12Sided` in `primaryContainer`, `warningContainer` or `errorContainer`
holding the loader or a 96dp glyph, and only its colour and content change between states.
`SetupActionBar` is the one button: a full pill with no bar surface and no divider, which is a
decided exception to the 20dp bar button below. Build a new setup screen out of these and the
hero, the title and the button land on the same rows as every other.

## When the rule does not fit

Sometimes it will not, and the answer is not to quietly use a different number.

Say it in the PR, and leave a comment at the site explaining what the deviation buys, the way the
action pill wrappers now do. An undocumented deviation is drift and the next contributor will
"fix" it. A documented one is a decision, and it survives.

## Decided exceptions

Looked at, deliberately left alone. Do not re-open these without a reason the original one
misses.

- **The FAB menu is elevated.** `ToggleFloatingActionButton` and `FloatingActionButtonMenuItem`
  take no elevation parameter and draw the Material shadow. A button floating over a scrolling
  list is the one place a shadow does structural work, and the extended FAB it replaced already
  shipped with its default elevation.
- **`TerminalDialog` and `ProgressDialog` do not use `DsDialog`.** The first is three quarters
  of the screen with its own header row, the second is not dismissible. If a third dialog ever
  wants either shape, that is the moment to widen the shared shell.
- **The init system status hues** (`statusColorFor()` in `ui/screen/InitServiceScreen.kt`) stay
  a hand picked traffic light rather than theme roles. Six of the seven states can sit in the
  legend row at once, and `primary` and `tertiary` move with dynamic colour, so running and
  abnormal would collapse onto nearly the same colour under some palettes.
- **The terminal virtual keys background** (`ui/screen/ContainerTerminalScreen.kt`) stays a
  literal. It sits against the terminal's own black, which does not follow the app theme, so a
  surface role would give a light strip under a dark terminal.
- **The setup flow's button** (`SetupActionBar` in `ui/component/SetupHero.kt`) is a full pill
  with no bar surface and no divider, not the 20dp bar button. It sits alone on the background
  under a hero rather than over scrolling content, and the pill is the Expressive button shape
  the welcome pages are built around. Its disabled colours also stay equal to its enabled
  colours: it is disabled only while a check or an install is in flight, and greying it out for
  that moment reads as a flicker.
- **The unit detail and override editor titles** keep a smaller style than every other screen
  title. Both display systemd unit names, which are long, so `titleLarge` would only ellipsize
  sooner.
- **`EmptyState` is not adopted** by `ui/screen/AutoBootPriorityScreen.kt` or the private empty
  state in `ui/screen/InitServiceScreen.kt`: absorbing them needs roughly seven new parameters
  for two call sites. `ui/component/ContainerUsersCard.kt` is not an empty state at all, just a
  plain `Text` in a card body.
- **Faint fill alphas of 0.03, 0.04 and 0.06** in the terminal dialog, the terminal screen and
  the sparse image screen stay unshared. They are invisible in isolation, and snapping them
  together would mean inventing a token for something nobody can see.
