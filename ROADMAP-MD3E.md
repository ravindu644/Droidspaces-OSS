# ROADMAP-MD3E.md

Temporary. This file tracks the conversion of the Android app to Material 3 Expressive. It is
linked from AGENTS.md so an agent picking up any UI task reads it first. The last item in the
ledger deletes this file and the AGENTS.md paragraph that points at it.

## Ground truth

- The agreed look: the design canvas https://claude.ai/artifact/Vgr8QxV79cJi1ch7LzPyEM
  (eleven boards: Home, Containers, Containers manage mode, Panel, container details, logs
  sheet, new session sheet, terminal, edit container, privileged sheet, settings). PNG
  exports at 3x were saved as `droidspaces-expressive-boards.zip` on 2026-10-09.
- The spec: [DESIGN.md](./DESIGN.md). Every value in the boards is written there with its
  token. When a board and DESIGN.md disagree, DESIGN.md wins and the board is stale.
- The stack: `androidx.compose.material3:material3:1.5.0-alpha18` over Compose BOM
  2026.06.01 (Compose 1.11.4), navigation-compose 2.7.7, Kotlin 2.2.21, compileSdk 36,
  AGP 8.11.1. Every Expressive API named in DESIGN.md exists in that jar
  (`MaterialExpressiveTheme`, `ButtonGroup`, `ToggleButton`, `SplitButton`,
  `HorizontalFloatingToolbar`, `ShortNavigationBar`, `LoadingIndicator`,
  `LinearWavyProgressIndicator`, flexible app bars, `MaterialShapes`, the Emphasized
  typography slots, the extended `Shapes` scale). Kotlin is not the gate. Moving to
  material3 beta or AGP 9 is a separate commit after the conversion, not part of it.
- To check an API before trusting a blog post: unzip `classes.jar` from the material3 aar
  under `~/.gradle/caches/modules-2` and `javap -p` the class. Dp constants are stored as
  doubles in the static initialiser (`ldc2_w ... // double 40.0d`).

## Rules of the road

- One phase per branch and one commit per phase, prefixed `app:`. Follow-ups squash into
  that commit while the branch is on `dev`.
- Build with `cd Android && ./build.sh`. Never gradle directly.
- Verify on a device: `adb devices` first, then walk the screen and `adb exec-out screencap`
  the states. Numbers in the boards are from a real phone (1080x2340); compare edges, not
  impressions.
- Resetting first-run state by editing the prefs XML as root needs `chown` back to the app
  uid, `chmod 660` and `restorecon`, and `setup_completed` plus `root_checked` restored,
  or the app lands on the welcome screen again.
- No new dependencies. material3 alpha18, navigation, lifecycle, libsu and the Termux
  terminal cover everything in the boards.
- Grep the CONTRIBUTING.md inventory before writing a component. The component ledger
  below says what each shared file becomes.
- DESIGN.md is the spec. If a screen needs something DESIGN.md does not cover, add the rule
  to DESIGN.md in the same commit, do not improvise at the site.

## Phase ledger

Tick a box only when the phase is built, on-device verified and committed.

### Phase 1, foundation
- [x] `ui/theme/Theme.kt`: `MaterialExpressiveTheme` in place of `MaterialTheme`, pass
      `shapes`; keep the palette builders and the AMOLED overrides as they are.
- [x] `ui/theme/Type.kt`: Roboto Flex, the full `Typography` constructor so every
      Emphasized slot is real, weights per the DESIGN.md table.
- [x] `ui/theme/Shape.kt` (new): the Expressive `Shapes` scale.
- [x] Delete `util/AnimationUtils.kt`. Callers to move onto `MaterialTheme.motionScheme`:
      `ui/navigation/DroidspacesNavigation.kt` (8), `ui/component/ContainerCard.kt` (3),
      `ui/component/RunningContainerCard.kt` (1), `ui/screen/ContainerDetailsScreen.kt` (5),
      `ui/screen/InitServiceScreen.kt` (2), `ui/component/ToggleCard.kt` (2),
      `ui/screen/ContainerTerminalScreen.kt` (2).
- [x] New shared pieces in `ui/component/`: `DetailPill.kt` (the pill in DESIGN.md),
      `StatusChip.kt` (replaces `StatusPill.kt`), `SegmentedGroup.kt` (rows with the 2dp
      gap and the AMOLED separator), `SectionLabel.kt` (replaces the styling in
      `SectionHeader.kt`), `Haptics.kt` (one helper over `LocalHapticFeedback`).
- [x] `ui/component/CardMetrics.kt`: header row 44dp, card padding 16 all sides.
- [x] Acceptance: the app builds and runs unchanged in look except type and shapes; no
      file outside `ui/theme/` names a hex colour that was not already a decided exception.

### Phase 2, app bars and navigation
- [ ] A shared small app bar composable: 64dp, glyph + title on the tabs, back + title on
      sub-screens, actions slot. Replace the title row in `ui/screen/MainTabScreen.kt` and
      every `TopAppBar` call in `ui/screen/`.
- [ ] `ShortNavigationBar` replaces `MainBottomBar` in `MainTabScreen.kt`; the pager uses
      `animateScrollToPage`; the `emptyStateBottomInset` plumbing goes with the overlay bar.
- [ ] Acceptance: Home, Containers and Panel content starts at the same y; the first card
      top edge is identical on all three.

### Phase 3, Home
- [ ] `DroidspacesStatusCard.kt`: `primaryContainer` card, 44dp header (verified badge,
      "Backend: Working", mode chip), divider, "Droidspaces" wordmark, "Version: vX".
- [ ] Count tiles in `MainTabScreen.kt`: `surfaceContainer`, no border, 24dp corners.
- [ ] `SystemInfoCard.kt` becomes a segmented group of five rows, no header.
- [ ] `HelpCard.kt` becomes one `tertiaryContainer` list row with a chevron.
- [ ] Acceptance: Home board 1 matches on device.

### Phase 4, Containers
- [ ] `ContainerCard.kt`: 44dp icon tile, name, status chip; divider; PID line when
      running; comma-joined metadata; option chips; connected button group (Start or Stop
      wide, Restart, Logs with `receipt_long`). No Open button: tap opens details when
      running, the card itself is the affordance.
- [ ] Manage mode: long press (or tap on a stopped card) outlines the card and opens an
      anchored `DropdownMenu` with Edit container configuration, Migrate to or Resize
      sparse image, Export container, Uninstall container. Replaces the in-card drawer.
- [ ] `TerminalDialog.kt` becomes a logs `ModalBottomSheet` for container operation logs;
      Copy logs filled, Clear outlined, title "Logs: <name>" with the receipt icon.
- [ ] FAB menu unchanged.
- [ ] Acceptance: boards 2 and 2b.

### Phase 5, Panel
- [ ] `RunningContainerCard.kt`: header with pretty name and chevron, divider, one
      `surfaceContainerHigh` box with uptime, IP, CPU bar, memory bar, then a full-width
      Terminal button. Card tap opens details.
- [ ] Acceptance: board 3.

### Phase 6, container details and terminal
- [ ] `ContainerDetailsScreen.kt`: back + name + refresh; sections Details, Usage,
      Available users, then the Terminal row and the init-system row, each with the detail
      pill. Refresh re-reads the user list.
- [ ] `ContainerUsersCard.kt`: toggle-button group of users, Copy login pill right aligned.
- [ ] `ContainerTerminalScreen.kt`: back and plus on one row, session tabs on the next;
      the user picker becomes a sheet listing users as rows with chevrons; virtual keys as
      two connected button groups with Ctrl and Alt as toggle buttons if the Termux
      `VirtualKeysView` can be replaced, otherwise restyled in place.
- [ ] Acceptance: boards 4, 6 and 7.

### Phase 7, edit and create forms
- [ ] `EditContainerScreen.kt`: small bar with a filled Save action, the running note as a
      calm info row, content in segmented groups.
- [ ] `ContainerConfigForm.kt`: network mode as a single-select connected button group;
      switch rows in segmented groups; `DsSlider.kt` on the Expressive `Slider`; disabled
      limits as rows that say why.
- [ ] `PrivilegedModeDialog.kt` becomes a sheet with the error banner, an "All flags" master
      row and the four flags.
- [ ] Boards first, then code, for `GatewaySettingsSection.kt`, `UpstreamInterfaceList.kt`,
      `PortForwardingList.kt`, `EnvironmentVariablesDialog.kt`, the bind mount dialog and
      `HardwareAccessDialog.kt`.
- [ ] Acceptance: boards 8 and 9, plus the new boards.

### Phase 8, the install wizard
- [ ] Boards first for `ContainerNameScreen`, `ContainerConfigScreen`,
      `SparseImageConfigScreen`, `InstallationSummaryScreen`, `InstallationProgressScreen`,
      `RootfsRepoSheet`, `FilePickerDialog`. The repo sheet keeps its wavy download bar.
- [ ] Code after the boards are agreed.

### Phase 9, settings and the rest
- [ ] `SettingsScreen.kt`, `TerminalAppearanceScreen.kt`: small bar, segmented groups,
      section labels (board 10 covers Settings).
- [ ] Boards first for `AutoBootPriorityScreen`, `RequirementsScreen`,
      `InitServiceScreen` and its wrappers, `JournaldScreen`, `UnitDetailScreen`,
      `OverrideEditorScreen`. The init-system traffic light stays.

### Phase 10, motion and haptics
- [ ] Navigation to 2.9 for predictive back; card to details as a shared element if it
      holds up; staggered list entry.
- [ ] Wire `Haptics.kt` per the DESIGN.md table.

### Phase 11, accessibility
- [ ] TalkBack names on every icon button and chip; font scaling on fixed-height frames
      (the setup frame, card headers); reduced motion respected.

### Phase 12, adaptive
- [ ] Decision pending (see Open questions). If yes: list-detail for containers, a
      navigation rail at medium width.

### Phase 13, close out
- [ ] Delete this file and the paragraph in AGENTS.md that links it. Update CONTRIBUTING.md's
      inventory for the new components.

## Per-screen decisions from the design session

Kept here so the reasoning is not lost with the chat.

- **Manage actions are a labelled anchored menu**, not a bottom sheet and not an icon
  toolbar. Material's guidance says an action sheet becomes a context menu on large windows
  and should never be full width there; Keep, Contacts and Messages use long press plus
  contextual actions instead of a per-item overflow; the user rejected the icon-only
  floating toolbar because the labels matter. Long press, or tap on a stopped card, mirrors
  the old drawer's gestures exactly.
- **No Open button on the container card.** Tapping a running card already opens details,
  and the Panel tab reaches the same screen. Stop or Start is the wide segment.
- **Panel and details overlap on purpose.** Panel is the live glance; details is the per
  container hub with users, terminal and init system. Both show the same stats box.
- **The detail pill is today's button**, measured from `ContainerUsersCard.kt:261` and
  `ContainerDetailsScreen.kt:454` and `:718`, because the three already matched and the
  Expressive pills tried first came out different sizes.
- **Logs use `receipt_long`.** There is no documented Material rule for a logs glyph;
  receipt_long is the long-record symbol Google surfaces use for activity, and it keeps
  Terminal's glyph unambiguous.
- **The Home status card** went through a clover hero, a KernelSU-style bright card, a
  cookie tile with a check and a bars glyph, and landed on today's structure in
  `primaryContainer`: verified badge, "Backend: Working", mode chip, divider, wordmark,
  version. Uppercase was tried and rejected.
- **Terminal header** drops the container name: the tab label already carries
  user@hostname, and the saved row gives the terminal room.
- **Live stats are plain bars.** Wavy indicators were drawn, then removed at the user's
  request.
- **Title case everywhere**, including chips and status text.

## Component ledger

Call sites are across `ui/`. Literal counts are radii / alpha fades / hex, and are what the
conversion removes.

| File | Purpose | Sites | Literals | Fate |
| --- | --- | --- | --- | --- |
| AccentColorPicker.kt | palette swatch row | 1 | 0/0/0 | keep, restyle as a segmented row |
| BugReportDialog.kt | bug report via TerminalDialog | 1 | 0/0/0 | follows TerminalDialog |
| CardMetrics.kt | shared card numbers | 9 | none | keep, update to 44dp header, 16 padding |
| ColorPaletteSwatch.kt | canvas swatch | 2 | 0/1/0 | keep |
| ContainerCard.kt | container list card | 3 | 5/16/0 | rewrite, phase 4 |
| ContainerConfigForm.kt | create and edit form | 2 | 4/6/0 | rewrite, phase 7 |
| ContainerUsersCard.kt | users and copy login | 1 | 2/1/0 | rewrite, phase 6 |
| DangerousActionConfirm.kt | warning card, phrase field | 4 | 2/2/0 | restyle (errorContainer banner) |
| DialogCloseButton.kt | 36dp close square | 2 | 1/0/0 | replace with a 48dp icon button |
| DialogFooterRow.kt | dialog actions | 19 | 1/6/0 | keep |
| DroidspacesStatusCard.kt | Home status card | 6 | 3/5/0 | rewrite, phase 3 |
| DsDialog.kt | dialog shell | 19 | 1/1/0 | keep |
| DsDropdown.kt | field plus menu | 5 | 1/0/0 | restyle on M3 ExposedDropdownMenu |
| DsMenuTheme.kt | menu theme and border | 8 | 1/1/0 | delete, M3 menu shapes |
| DsSlider.kt | bar-thumb slider | 3 | 0/0/0 | restyle on the Expressive Slider |
| DsSnackbarHost.kt | snackbar host | 6 | 0/0/0 | keep |
| DsTextFieldDefaults.kt | text field colours | 14 | 0/7/0 | replace with M3 defaults |
| EmptyState.kt | empty, error, root, kernel states | 13 | 1/4/0 | restyle |
| EnvironmentVariablesDialog.kt | env editor | 1 | 1/2/0 | sheet, phase 7 |
| FilePickerDialog.kt | root file browser | 2 | 1/1/0 | restyle, phase 8 |
| GatewaySettingsSection.kt | gateway block and dialog | 1 | 1/0/0 | board then sheet, phase 7 |
| HardwareAccessDialog.kt | typed confirmation | 2 | 0/0/0 | keep on DsDialog |
| HelpCard.kt | help promo | 1 | 1/2/0 | one tertiary row, phase 3 |
| MorphShape.kt | morph between MaterialShapes | 1 | none | keep |
| PermissionRequest.kt | runtime permission flow | 2 | none | keep |
| PortForwardingList.kt | port list and dialog | 1 | 4/3/0 | board then rewrite, phase 7 |
| PrimaryActionBottomBar.kt | full-width CTA bar | 7 | 1/2/0 | keep for wizard, setup variant stays |
| PrivilegedModeDialog.kt | privileged flags | 2 | 0/1/0 | sheet, phase 7 |
| PullToRefreshWrapper.kt | pull to refresh | 4 | 0/0/0 | keep, add the haptic |
| RootfsRepoSheet.kt | rootfs repo sheet | 2 | 10/32/0 | board then rewrite, phase 8 |
| RunningContainerCard.kt | Panel card | 1 | 4/6/0 | rewrite, phase 5 |
| SaveActionBottomBar.kt | save bar | 2 | 0/2/0 | replace with Save in the app bar |
| SectionHeader.kt | group heading | 20 | 0/0/0 | restyle as SectionLabel |
| SettingsCard.kt | bordered card shell | 7 | 1/1/0 | replace with segmented rows |
| SettingsRowCard.kt | clickable SettingsCard | 4 | 0/2/0 | replace with ListItem rows |
| SetupHero.kt | setup flow geometry | 16 | 0/0/0 | keep |
| StatusPill.kt | dot plus label | 4 | 2/2/0 | replace with StatusChip |
| SwitchItem.kt | switch row | 13 | 0/2/0 | keep, drop alpha fades |
| SystemInfoCard.kt | device info | 2 | 1/2/0 | segmented group, phase 3 |
| TerminalConsole.kt | ANSI console | 3 | 3/8/0 | restyle on surfaceContainerLowest |
| TerminalDialog.kt | log viewer | 4 | 2/10/0 | becomes the logs sheet, phase 4 |
| TerminalFontDialog.kt | font picker | 1 | 2/8/0 | restyle, phase 9 |
| ToggleCard.kt | card with a switch | 22 | 0/4/0 | replace with switch rows in a group |
| UpstreamInterfaceList.kt | upstream list and dialog | 1 | 4/2/0 | board then rewrite, phase 7 |

Repo-wide under `ui/` before the conversion: 129 radius literals, 288 alpha fades, 75
borders, 34 `tonalElevation`, 103 custom `Surface`s and no M3 `Card`; 76 hex literals of
which 69 are the theme files, 6 the init-system hues and 1 the terminal keys colour. No
Compose haptics anywhere.

## Open questions

- **Accent picker and AMOLED.** Keep both (the plan assumes yes, and DESIGN.md writes the
  AMOLED rules), or go dynamic colour only like Google's apps? Decide before phase 3, since
  the status card's AMOLED treatment depends on it.
- **Tablet and foldable.** In or out of scope? It decides whether phase 12 exists.
- **Terminal keys.** The Termux `VirtualKeysView` is a View; replacing it with Compose
  toggle buttons means owning key repeat and modifier latching. Decide in phase 6 after a
  spike.
