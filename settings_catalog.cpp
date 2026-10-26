#include "settings_catalog.h"

#include <QCoreApplication>
#include <QHash>
#include <QRegularExpression>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace jarvis::settings {
namespace {

struct Entry
{
    const char *id;
    const char *page;
    const char *group;
    const char *title;
    const char *description;
    // Comma separated; translated with synonyms of the target language.
    const char *synonyms;
    const char *configKey;
    const char *kcm;
};

// The order is the order of the pages and rows in the window.
const Entry kEntries[] = {
    // ---- Desktop ----
    {"desktop.mode", "desktop", QT_TRANSLATE_NOOP("SettingsCatalog", "JARVIS DESKTOP"), QT_TRANSLATE_NOOP("SettingsCatalog", "JARVIS DESKTOP or standard KDE"),
     QT_TRANSLATE_NOOP("SettingsCatalog", "Turn the Jarvis desktop on after a check and a backup, or go back to standard KDE."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "desktop mode, shell, layout, standard kde, plasma"), "desktop.mode", ""},
    {"desktop.theme", "desktop", QT_TRANSLATE_NOOP("SettingsCatalog", "Appearance"), QT_TRANSLATE_NOOP("SettingsCatalog", "Theme"), QT_TRANSLATE_NOOP("SettingsCatalog", "Classic JARVIS, Mark VII or Minimal."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "look, style, design, appearance, skin"), "desktop.theme", ""},
    {"desktop.colors", "desktop", QT_TRANSLATE_NOOP("SettingsCatalog", "Appearance"), QT_TRANSLATE_NOOP("SettingsCatalog", "Primary and secondary colour"), QT_TRANSLATE_NOOP("SettingsCatalog", "Your own colours instead of the theme's."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "colour, color, accent, palette"), "desktop.primary", ""},
    {"desktop.glow", "desktop", QT_TRANSLATE_NOOP("SettingsCatalog", "Appearance"), QT_TRANSLATE_NOOP("SettingsCatalog", "Glow"), QT_TRANSLATE_NOOP("SettingsCatalog", "How bright the holographic edges shine."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "brightness, light, neon"), "desktop.glow", ""},
    {"desktop.opacity", "desktop", QT_TRANSLATE_NOOP("SettingsCatalog", "Appearance"), QT_TRANSLATE_NOOP("SettingsCatalog", "Panel transparency"), QT_TRANSLATE_NOOP("SettingsCatalog", "How much of the wallpaper shows through panels and widgets."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "transparent, opacity, glass, see through"), "desktop.opacity", ""},
    {"desktop.motion", "desktop", QT_TRANSLATE_NOOP("SettingsCatalog", "Appearance"), QT_TRANSLATE_NOOP("SettingsCatalog", "Animation speed"), QT_TRANSLATE_NOOP("SettingsCatalog", "Slower or faster rings and transitions."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "animations, motion, speed, movement"), "desktop.motion", ""},
    {"desktop.density", "desktop", QT_TRANSLATE_NOOP("SettingsCatalog", "Appearance"), QT_TRANSLATE_NOOP("SettingsCatalog", "Size of Jarvis elements"), QT_TRANSLATE_NOOP("SettingsCatalog", "Compact, normal or comfortable spacing in the widgets and the dock."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "scale, zoom, size, bigger, smaller, density, spacing"), "desktop.density", ""},
    {"desktop.quality", "desktop", QT_TRANSLATE_NOOP("SettingsCatalog", "Appearance"), QT_TRANSLATE_NOOP("SettingsCatalog", "Effects quality on the desktop"), QT_TRANSLATE_NOOP("SettingsCatalog", "Fewer effects use less of the graphics card."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "performance, gpu, effects, battery, quality"), "desktop.quality", ""},
    {"desktop.core", "desktop", QT_TRANSLATE_NOOP("SettingsCatalog", "Appearance"), QT_TRANSLATE_NOOP("SettingsCatalog", "Core size and place"), QT_TRANSLATE_NOOP("SettingsCatalog", "The Jarvis core on the desktop: small, medium or large; centre, left or right."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "core, reactor, centre, size, position"), "desktop.coreSize", ""},
    {"window.hud", "desktop", QT_TRANSLATE_NOOP("SettingsCatalog", "Jarvis window"), QT_TRANSLATE_NOOP("SettingsCatalog", "Look of the Jarvis window"), QT_TRANSLATE_NOOP("SettingsCatalog", "The animated HUD with every effect, lighter versions, or the classic chat."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "hud, cinematic, classic chat, window, interface"), "", ""},
    {"dock.position", "desktop", QT_TRANSLATE_NOOP("SettingsCatalog", "Dock"), QT_TRANSLATE_NOOP("SettingsCatalog", "Dock position"), QT_TRANSLATE_NOOP("SettingsCatalog", "Bottom, top, left or right edge of the screen."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "dock, panel, taskbar, edge, place, position"), "desktop.dockPosition", ""},
    {"dock.size", "desktop", QT_TRANSLATE_NOOP("SettingsCatalog", "Dock"), QT_TRANSLATE_NOOP("SettingsCatalog", "Dock height"), QT_TRANSLATE_NOOP("SettingsCatalog", "The thickness of the dock panel in pixels."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "dock, panel, taskbar, size, height, thickness, bigger, smaller"), "desktop.dockSize", ""},
    {"dock.alignment", "desktop", QT_TRANSLATE_NOOP("SettingsCatalog", "Dock"), QT_TRANSLATE_NOOP("SettingsCatalog", "Dock alignment"), QT_TRANSLATE_NOOP("SettingsCatalog", "Centred, at the start or at the end of its edge."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "dock, panel, align, centre"), "desktop.dockAlignment", ""},
    {"dock.autohide", "desktop", QT_TRANSLATE_NOOP("SettingsCatalog", "Dock"), QT_TRANSLATE_NOOP("SettingsCatalog", "Show the dock automatically"), QT_TRANSLATE_NOOP("SettingsCatalog", "The dock stays hidden and slides in when the pointer touches the screen edge."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "autohide, hide, auto hide, dock, panel"), "desktop.dockAutohide", ""},
    {"dock.icons", "desktop", QT_TRANSLATE_NOOP("SettingsCatalog", "Dock"), QT_TRANSLATE_NOOP("SettingsCatalog", "Icon size in the dock"), QT_TRANSLATE_NOOP("SettingsCatalog", "How much of the dock's height an icon takes."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "icons, dock, size, bigger"), "", ""},
    {"dock.magnify", "desktop", QT_TRANSLATE_NOOP("SettingsCatalog", "Dock"), QT_TRANSLATE_NOOP("SettingsCatalog", "Magnify icons under the pointer"), QT_TRANSLATE_NOOP("SettingsCatalog", "Icons grow when the pointer passes over them."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "zoom, magnification, enlarge, hover, dock"), "", ""},
    {"dock.glass", "desktop", QT_TRANSLATE_NOOP("SettingsCatalog", "Dock"), QT_TRANSLATE_NOOP("SettingsCatalog", "Jarvis glass under the whole panel"), QT_TRANSLATE_NOOP("SettingsCatalog", "The dock, the system tray and the clock on one dark glass."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "glass, background, tray, clock, panel style"), "", ""},
    {"dock.parts", "desktop", QT_TRANSLATE_NOOP("SettingsCatalog", "Dock"), QT_TRANSLATE_NOOP("SettingsCatalog", "What the dock shows"), QT_TRANSLATE_NOOP("SettingsCatalog", "Workspace buttons, virtual desktops, the Jarvis button, one button per application."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "workspaces, desktops, group windows, buttons, dock"), "", ""},
    {"desktop.replacePanels", "desktop", QT_TRANSLATE_NOOP("SettingsCatalog", "Panels and widgets"), QT_TRANSLATE_NOOP("SettingsCatalog", "Replace the standard panels with the dock"),
     QT_TRANSLATE_NOOP("SettingsCatalog", "When JARVIS DESKTOP is turned on: remove your panels, or keep them beside the dock."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "panel, taskbar, replace, keep panels"), "desktop.replacePanels", ""},
    {"desktop.effects", "desktop", QT_TRANSLATE_NOOP("SettingsCatalog", "Panels and widgets"), QT_TRANSLATE_NOOP("SettingsCatalog", "Animations in the desktop widgets"), QT_TRANSLATE_NOOP("SettingsCatalog", "Off: the core and the widgets stand still and only update their numbers."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "animations, effects, widgets, still"), "desktop.effects", ""},
    {"widgets.add", "desktop", QT_TRANSLATE_NOOP("SettingsCatalog", "Panels and widgets"), QT_TRANSLATE_NOOP("SettingsCatalog", "Desktop widgets"), QT_TRANSLATE_NOOP("SettingsCatalog", "Add or remove the System, Network, Power, Media and Status widgets."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "widget, plasmoid, applet, add widget, cpu, battery, media"), "", ""},
    {"widgets.hud", "desktop", QT_TRANSLATE_NOOP("SettingsCatalog", "Panels and widgets"), QT_TRANSLATE_NOOP("SettingsCatalog", "Jarvis HUD widget"), QT_TRANSLATE_NOOP("SettingsCatalog", "A widget with Jarvis's state, system activity and the last notification."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "hud widget, desktop widget, plasmoid"), "", ""},
    {"widgets.animation", "desktop", QT_TRANSLATE_NOOP("SettingsCatalog", "Panels and widgets"), QT_TRANSLATE_NOOP("SettingsCatalog", "HUD widget animations"), QT_TRANSLATE_NOOP("SettingsCatalog", "Off, low, normal or high; one step lower on battery."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "animations, widget, battery, intensity"), "hud_animation", ""},
    {"workspaces.profiles", "desktop", QT_TRANSLATE_NOOP("SettingsCatalog", "Workspaces"), QT_TRANSLATE_NOOP("SettingsCatalog", "Workspaces"), QT_TRANSLATE_NOOP("SettingsCatalog", "GENERAL, STUDY, WORK and DEV: a virtual desktop, applications, files and widgets each."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "profiles, virtual desktops, general, study, work, dev, prepare"), "", ""},

    // ---- Conversation ----
    {"language.ui", "conversation", QT_TRANSLATE_NOOP("SettingsCatalog", "Language"), QT_TRANSLATE_NOOP("SettingsCatalog", "Interface language"), QT_TRANSLATE_NOOP("SettingsCatalog", "The language of Jarvis's windows and messages."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "language, russian, english, locale, translation"), "language", ""},
    {"language.reply", "conversation", QT_TRANSLATE_NOOP("SettingsCatalog", "Language"), QT_TRANSLATE_NOOP("SettingsCatalog", "Answer language"), QT_TRANSLATE_NOOP("SettingsCatalog", "The same as your message, or always one language."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "language, reply, answer, russian, english"), "reply_language", ""},
    {"voice.speak", "conversation", QT_TRANSLATE_NOOP("SettingsCatalog", "Voice"), QT_TRANSLATE_NOOP("SettingsCatalog", "Speak answers aloud"), QT_TRANSLATE_NOOP("SettingsCatalog", "Jarvis reads its answers out; code and links are skipped."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "voice, speech, read aloud, tts, sound, talk"), "", ""},
    {"voice.engine", "conversation", QT_TRANSLATE_NOOP("SettingsCatalog", "Voice"), QT_TRANSLATE_NOOP("SettingsCatalog", "Voice engine"), QT_TRANSLATE_NOOP("SettingsCatalog", "ElevenLabs in the cloud, Chatterbox or Piper on this computer, or the system voice."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "voice, engine, piper, chatterbox, elevenlabs, espeak"), "", ""},
    {"voice.elevenlabs", "conversation", QT_TRANSLATE_NOOP("SettingsCatalog", "Voice"), QT_TRANSLATE_NOOP("SettingsCatalog", "ElevenLabs key"), QT_TRANSLATE_NOOP("SettingsCatalog", "Studio voices in the cloud need an ElevenLabs API key."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "api key, elevenlabs, token, key"), "", ""},
    {"voice.pace", "conversation", QT_TRANSLATE_NOOP("SettingsCatalog", "Voice"), QT_TRANSLATE_NOOP("SettingsCatalog", "Speaking pace"), QT_TRANSLATE_NOOP("SettingsCatalog", "Slower or faster speech."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "speed, rate, tempo, pace, voice"), "", ""},
    {"voice.dictation", "conversation", QT_TRANSLATE_NOOP("SettingsCatalog", "Microphone"), QT_TRANSLATE_NOOP("SettingsCatalog", "Dictation"), QT_TRANSLATE_NOOP("SettingsCatalog", "Speak instead of typing: recognised on this computer (Vosk)."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "microphone, mic, speech to text, voice input, recognition, headset"), "", ""},
    {"shortcuts.overlay", "conversation", QT_TRANSLATE_NOOP("SettingsCatalog", "Keyboard shortcuts"), QT_TRANSLATE_NOOP("SettingsCatalog", "Key for the overlay"), QT_TRANSLATE_NOOP("SettingsCatalog", "Opens Jarvis above any application (Ctrl+J by default)."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "shortcut, hotkey, keys, buttons, combination, keyboard, ctrl j, overlay"), "", ""},
    {"shortcuts.quick", "conversation", QT_TRANSLATE_NOOP("SettingsCatalog", "Keyboard shortcuts"), QT_TRANSLATE_NOOP("SettingsCatalog", "Key for quick commands"), QT_TRANSLATE_NOOP("SettingsCatalog", "Opens the quick command bar (Meta+J by default)."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "shortcut, hotkey, keys, buttons, combination, keyboard, meta j, quick"), "", ""},
    {"shortcuts.window", "conversation", QT_TRANSLATE_NOOP("SettingsCatalog", "Keyboard shortcuts"), QT_TRANSLATE_NOOP("SettingsCatalog", "Keys inside the Jarvis window"), QT_TRANSLATE_NOOP("SettingsCatalog", "Ctrl+M, Ctrl+Shift+O and the chat keys; these are fixed."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "shortcut, hotkey, keys, buttons, combination, keyboard"), "", ""},
    {"chat.notify", "conversation", QT_TRANSLATE_NOOP("SettingsCatalog", "Chat and overlay"), QT_TRANSLATE_NOOP("SettingsCatalog", "Tell me when an answer is ready while Jarvis is hidden"), QT_TRANSLATE_NOOP("SettingsCatalog", "A notification with the answer you were waiting for."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "notification, notify, answer, hidden, alert"), "notify_replies", ""},
    {"chat.overlay", "conversation", QT_TRANSLATE_NOOP("SettingsCatalog", "Chat and overlay"), QT_TRANSLATE_NOOP("SettingsCatalog", "Compact core above other windows"), QT_TRANSLATE_NOOP("SettingsCatalog", "A small always-on-top core that shows what Jarvis is doing."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "overlay, always on top, compact, floating"), "", ""},
    {"reminders.snooze", "conversation", QT_TRANSLATE_NOOP("SettingsCatalog", "Reminders"), QT_TRANSLATE_NOOP("SettingsCatalog", "\"Snooze\" button"), QT_TRANSLATE_NOOP("SettingsCatalog", "How long a snoozed reminder waits."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "snooze, later, postpone, reminder"), "snooze_minutes", ""},
    {"reminders.repeat", "conversation", QT_TRANSLATE_NOOP("SettingsCatalog", "Reminders"), QT_TRANSLATE_NOOP("SettingsCatalog", "Remind again until I press \"Done\""), QT_TRANSLATE_NOOP("SettingsCatalog", "Once, or again every few minutes, up to three times."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "nag, repeat, again, reminder"), "nag_minutes", ""},
    {"reminders.times", "conversation", QT_TRANSLATE_NOOP("SettingsCatalog", "Reminders"), QT_TRANSLATE_NOOP("SettingsCatalog", "What \"morning\", \"afternoon\" and \"evening\" mean"), QT_TRANSLATE_NOOP("SettingsCatalog", "The hours used for \"remind me in the evening\"."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "morning, evening, afternoon, hours, time"), "morning_hour", ""},
    {"reminders.sound", "conversation", QT_TRANSLATE_NOOP("SettingsCatalog", "Reminders"), QT_TRANSLATE_NOOP("SettingsCatalog", "Sound with reminders"), QT_TRANSLATE_NOOP("SettingsCatalog", "The alarm sound of your Plasma sound theme."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "sound, alarm, beep, audio, reminder"), "reminder_sound", ""},

    // ---- Intelligence ----
    {"ai.key", "intelligence", QT_TRANSLATE_NOOP("SettingsCatalog", "Claude"), QT_TRANSLATE_NOOP("SettingsCatalog", "Claude API key"), QT_TRANSLATE_NOOP("SettingsCatalog", "Needed for answers beyond local commands; stored in the system password store."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "key, api, token, anthropic, connection, account, password, provider"), "", ""},
    {"ai.model", "intelligence", QT_TRANSLATE_NOOP("SettingsCatalog", "Claude"), QT_TRANSLATE_NOOP("SettingsCatalog", "Model"), QT_TRANSLATE_NOOP("SettingsCatalog", "Haiku is fast and inexpensive, Sonnet and Opus are stronger."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "model, haiku, sonnet, opus, llm, ai, provider"), "model", ""},
    {"ai.test", "intelligence", QT_TRANSLATE_NOOP("SettingsCatalog", "Claude"), QT_TRANSLATE_NOOP("SettingsCatalog", "Check the connection"), QT_TRANSLATE_NOOP("SettingsCatalog", "Checks the key and the model with Anthropic; costs nothing."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "test, connection, check, internet, network, online, connect"), "", ""},
    {"ai.websearch", "intelligence", QT_TRANSLATE_NOOP("SettingsCatalog", "Claude"), QT_TRANSLATE_NOOP("SettingsCatalog", "Let Claude search the web for current facts"), QT_TRANSLATE_NOOP("SettingsCatalog", "Up to three web searches per answer, billed per search by Anthropic."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "web search, internet, google, current, news"), "web_search", ""},
    {"ai.offline", "intelligence", QT_TRANSLATE_NOOP("SettingsCatalog", "Without a key or internet"), QT_TRANSLATE_NOOP("SettingsCatalog", "What works without a key or internet"), QT_TRANSLATE_NOOP("SettingsCatalog", "Local commands, calculators, skills' ready answers, reminders and dictation."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "offline, local, without internet, fallback, no key"), "", ""},

    // ---- Memory and privacy ----
    {"data.storage", "privacy", QT_TRANSLATE_NOOP("SettingsCatalog", "Overview"), QT_TRANSLATE_NOOP("SettingsCatalog", "What is stored and what is sent"), QT_TRANSLATE_NOOP("SettingsCatalog", "What stays on this computer and what goes to Anthropic and ElevenLabs."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "data, privacy, local, cloud, sent, anthropic, gdpr"), "", ""},
    {"memory.learn", "privacy", QT_TRANSLATE_NOOP("SettingsCatalog", "Memory"), QT_TRANSLATE_NOOP("SettingsCatalog", "Learn from our conversations"), QT_TRANSLATE_NOOP("SettingsCatalog", "Facts, topics and corrections from the chat; passwords and card numbers never."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "remember, memorize, memory, learning, history, facts"), "learn_dialog", ""},
    {"memory.curiosity", "privacy", QT_TRANSLATE_NOOP("SettingsCatalog", "Memory"), QT_TRANSLATE_NOOP("SettingsCatalog", "Jarvis asks questions to get to know me"), QT_TRANSLATE_NOOP("SettingsCatalog", "Now and then, never in the middle of a task."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "curiosity, questions, ask"), "curiosity", ""},
    {"memory.reflection", "privacy", QT_TRANSLATE_NOOP("SettingsCatalog", "Memory"), QT_TRANSLATE_NOOP("SettingsCatalog", "Review my memory every day"), QT_TRANSLATE_NOOP("SettingsCatalog", "Once a day Claude tidies the facts: merges duplicates, drops outdated ones."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "reflection, review, tidy, memory, summary"), "deep_reflection", ""},
    {"memory.panel", "privacy", QT_TRANSLATE_NOOP("SettingsCatalog", "Memory"), QT_TRANSLATE_NOOP("SettingsCatalog", "Review what Jarvis remembers"), QT_TRANSLATE_NOOP("SettingsCatalog", "The memory panel: facts, topics, tasks, conversations."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "memory, facts, view, remembered, know about me"), "", ""},
    {"activity.track", "privacy", QT_TRANSLATE_NOOP("SettingsCatalog", "Activity"), QT_TRANSLATE_NOOP("SettingsCatalog", "See which application I'm working in"), QT_TRANSLATE_NOOP("SettingsCatalog", "Time per application and habits, kept for 14 days."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "activity, tracking, applications, time, habits, history"), "track_activity", ""},
    {"activity.titles", "privacy", QT_TRANSLATE_NOOP("SettingsCatalog", "Activity"), QT_TRANSLATE_NOOP("SettingsCatalog", "Also keep window titles"), QT_TRANSLATE_NOOP("SettingsCatalog", "Titles show the document or page; private windows are skipped."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "window titles, documents, pages"), "track_titles", ""},
    {"activity.share", "privacy", QT_TRANSLATE_NOOP("SettingsCatalog", "Activity"), QT_TRANSLATE_NOOP("SettingsCatalog", "Tell Claude what I'm doing"), QT_TRANSLATE_NOOP("SettingsCatalog", "Adds the current application and today's top apps to requests."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "share, context, send activity"), "share_activity", ""},
    {"awareness.window", "privacy", QT_TRANSLATE_NOOP("SettingsCatalog", "Context sources"), QT_TRANSLATE_NOOP("SettingsCatalog", "Allow Jarvis to see the active window"), QT_TRANSLATE_NOOP("SettingsCatalog", "The application and title of the window you work in, kept in memory only."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "window, application, title, focus, context"), "awareness_window", ""},
    {"awareness.screen", "privacy", QT_TRANSLATE_NOOP("SettingsCatalog", "Context sources"), QT_TRANSLATE_NOOP("SettingsCatalog", "Allow analysis of the screen content"), QT_TRANSLATE_NOOP("SettingsCatalog", "A screenshot read on this computer when you ask; never saved."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "screen, screenshot, ocr, see, look, read screen, capture"), "awareness_screen", ""},
    {"awareness.cloud", "privacy", QT_TRANSLATE_NOOP("SettingsCatalog", "Context sources"), QT_TRANSLATE_NOOP("SettingsCatalog", "Send screenshots to Claude"), QT_TRANSLATE_NOOP("SettingsCatalog", "A question about the screen also carries the picture to Anthropic."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "screenshot, image, picture, cloud, send"), "awareness_cloud_images", ""},
    {"awareness.interval", "privacy", QT_TRANSLATE_NOOP("SettingsCatalog", "Context sources"), QT_TRANSLATE_NOOP("SettingsCatalog", "Background screen analysis"), QT_TRANSLATE_NOOP("SettingsCatalog", "Never, or at most every few minutes; only with offers of help on."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "background, interval, periodic, screen"), "screen_interval_minutes", ""},
    {"awareness.excluded", "privacy", QT_TRANSLATE_NOOP("SettingsCatalog", "Context sources"), QT_TRANSLATE_NOOP("SettingsCatalog", "Never observe these applications"), QT_TRANSLATE_NOOP("SettingsCatalog", "Applications Jarvis never looks at."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "exclude, ignore, blacklist, private, applications"), "awareness_excluded", ""},
    {"awareness.pause", "privacy", QT_TRANSLATE_NOOP("SettingsCatalog", "Context sources"), QT_TRANSLATE_NOOP("SettingsCatalog", "Pause observation"), QT_TRANSLATE_NOOP("SettingsCatalog", "For an hour or until tomorrow."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "pause, stop, suspend, privacy"), "", ""},
    {"data.delete", "privacy", QT_TRANSLATE_NOOP("SettingsCatalog", "Saved data"), QT_TRANSLATE_NOOP("SettingsCatalog", "Delete saved data"), QT_TRANSLATE_NOOP("SettingsCatalog", "Activity history, facts and conversations, code lessons, driving progress, observed context."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "delete, erase, forget, clear, history, wipe, remove data"), "", ""},

    // ---- Actions and permissions ----
    {"awareness.level", "actions", QT_TRANSLATE_NOOP("SettingsCatalog", "Initiative"), QT_TRANSLATE_NOOP("SettingsCatalog", "How much initiative Jarvis takes"), QT_TRANSLATE_NOOP("SettingsCatalog", "Off, only when asked, rare offers or more offers of help."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "autonomy, level, proactive, assistive, initiative, manual"), "awareness_level", ""},
    {"offers.limits", "actions", QT_TRANSLATE_NOOP("SettingsCatalog", "Offers of help"), QT_TRANSLATE_NOOP("SettingsCatalog", "How often Jarvis may offer help"), QT_TRANSLATE_NOOP("SettingsCatalog", "At most one offer every so often, and a limit per day."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "limit, cooldown, frequency, offers, suggestions"), "suggestion_cooldown_minutes", ""},
    {"offers.categories", "actions", QT_TRANSLATE_NOOP("SettingsCatalog", "Offers of help"), QT_TRANSLATE_NOOP("SettingsCatalog", "Kinds of offers"), QT_TRANSLATE_NOOP("SettingsCatalog", "Build errors, driving test questions, electronics, documentation, studying."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "categories, offers, suggestions, kinds"), "awareness_muted", ""},
    {"offers.fullscreen", "actions", QT_TRANSLATE_NOOP("SettingsCatalog", "Offers of help"), QT_TRANSLATE_NOOP("SettingsCatalog", "Also in fullscreen applications"), QT_TRANSLATE_NOOP("SettingsCatalog", "Off: games, presentations and films are never interrupted."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "fullscreen, games, films, presentation, interrupt"), "awareness_fullscreen", ""},
    {"offers.stats", "actions", QT_TRANSLATE_NOOP("SettingsCatalog", "Offers of help"), QT_TRANSLATE_NOOP("SettingsCatalog", "Offers in the last 30 days"), QT_TRANSLATE_NOOP("SettingsCatalog", "How many offers were shown, accepted and declined."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "log, history, journal, statistics, offers"), "", ""},
    {"proactive.speak", "actions", QT_TRANSLATE_NOOP("SettingsCatalog", "Jarvis speaks up"), QT_TRANSLATE_NOOP("SettingsCatalog", "Jarvis speaks up by itself"), QT_TRANSLATE_NOOP("SettingsCatalog", "Hints in notifications: a break, words to review, a question."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "proactive, hints, notifications, speak up"), "proactive", ""},
    {"proactive.breaks", "actions", QT_TRANSLATE_NOOP("SettingsCatalog", "Jarvis speaks up"), QT_TRANSLATE_NOOP("SettingsCatalog", "Suggest a break"), QT_TRANSLATE_NOOP("SettingsCatalog", "After an hour or two at the computer."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "break, rest, pause, health"), "break_minutes", ""},
    {"proactive.morning", "actions", QT_TRANSLATE_NOOP("SettingsCatalog", "Jarvis speaks up"), QT_TRANSLATE_NOOP("SettingsCatalog", "Morning plan"), QT_TRANSLATE_NOOP("SettingsCatalog", "Today's reminders, tasks and flashcards when you sit down."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "morning, plan, summary, day"), "morning_summary", ""},
    {"actions.integration", "actions", QT_TRANSLATE_NOOP("SettingsCatalog", "Permissions"), QT_TRANSLATE_NOOP("SettingsCatalog", "Let widgets and KRunner use Jarvis"), QT_TRANSLATE_NOOP("SettingsCatalog", "They may ask questions and start the actions Jarvis lists; never commands or memory."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "krunner, widgets, integration, plasma, permissions"), "desktop_integration", ""},
    {"actions.activeApp", "actions", QT_TRANSLATE_NOOP("SettingsCatalog", "Permissions"), QT_TRANSLATE_NOOP("SettingsCatalog", "Tell desktop components the active application"), QT_TRANSLATE_NOOP("SettingsCatalog", "Its name only, never the title."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "active application, share, widgets"), "share_active_app", ""},
    {"actions.confirm", "actions", QT_TRANSLATE_NOOP("SettingsCatalog", "Permissions"), QT_TRANSLATE_NOOP("SettingsCatalog", "What Jarvis always asks before doing"), QT_TRANSLATE_NOOP("SettingsCatalog", "Changes to the desktop, deleting data, installing; administrator passwords go to the system."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "confirm, ask, permission, approve, admin, password, sudo"), "", ""},

    // ---- Skills ----
    {"skills.list", "skills", QT_TRANSLATE_NOOP("SettingsCatalog", "Skills"), QT_TRANSLATE_NOOP("SettingsCatalog", "Skills"), QT_TRANSLATE_NOOP("SettingsCatalog", "Topics with calculators, formulas and ready answers that work without the internet."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "skills, topics, calculators, formulas, knowledge, electronics, physics"), "", ""},
    {"skills.import", "skills", QT_TRANSLATE_NOOP("SettingsCatalog", "Skills"), QT_TRANSLATE_NOOP("SettingsCatalog", "Import a skill"), QT_TRANSLATE_NOOP("SettingsCatalog", "From a JSON file or a link."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "import, install skill, json, download"), "", ""},

    // ---- System and recovery ----
    {"system.autostart", "system", QT_TRANSLATE_NOOP("SettingsCatalog", "Start"), QT_TRANSLATE_NOOP("SettingsCatalog", "Start Jarvis when I log in"), QT_TRANSLATE_NOOP("SettingsCatalog", "Jarvis waits in the system tray and comes back if it crashes."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "autostart, login, startup, boot, tray, start"), "", ""},
    {"system.background", "system", QT_TRANSLATE_NOOP("SettingsCatalog", "Start"), QT_TRANSLATE_NOOP("SettingsCatalog", "Keep reminders working when the window is closed"), QT_TRANSLATE_NOOP("SettingsCatalog", "The service keeps reminders, hints, KRunner and widgets running."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "background, service, daemon, reminders, closed window"), "background", ""},
    {"system.version", "system", QT_TRANSLATE_NOOP("SettingsCatalog", "Status"), QT_TRANSLATE_NOOP("SettingsCatalog", "Versions and services"), QT_TRANSLATE_NOOP("SettingsCatalog", "Jarvis, Qt and Plasma versions, the background service and the password store."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "version, about, status, service, daemon, jarvisd"), "", ""},
    {"system.diagnostics", "system", QT_TRANSLATE_NOOP("SettingsCatalog", "Status"), QT_TRANSLATE_NOOP("SettingsCatalog", "Diagnostics"), QT_TRANSLATE_NOOP("SettingsCatalog", "Checks the service, its systemd units, Plasma, widgets and KRunner."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "diagnose, diagnostics, problems, report, log, journal, troubleshoot"), "", ""},
    {"system.graphics", "system", QT_TRANSLATE_NOOP("SettingsCatalog", "Status"), QT_TRANSLATE_NOOP("SettingsCatalog", "Graphics card and API"), QT_TRANSLATE_NOOP("SettingsCatalog", "Which GPU and API draw Jarvis; applies after a restart of Jarvis."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "gpu, video card, nvidia, amd, intel, vulkan, opengl, render"), "", ""},
    {"system.update", "system", QT_TRANSLATE_NOOP("SettingsCatalog", "Status"), QT_TRANSLATE_NOOP("SettingsCatalog", "Updates"), QT_TRANSLATE_NOOP("SettingsCatalog", "Download a verified package from GitHub Releases."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "update, upgrade, new version, release"), "", ""},
    {"system.service", "system", QT_TRANSLATE_NOOP("SettingsCatalog", "Status"), QT_TRANSLATE_NOOP("SettingsCatalog", "Background service"), QT_TRANSLATE_NOOP("SettingsCatalog", "Restart or stop jarvisd."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "service, daemon, jarvisd, restart, stop"), "", ""},
    {"kde.sound", "system", QT_TRANSLATE_NOOP("SettingsCatalog", "KDE System Settings"), QT_TRANSLATE_NOOP("SettingsCatalog", "Sound: speakers, headphones and microphone"), QT_TRANSLATE_NOOP("SettingsCatalog", "Opens System Settings → Sound."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "headphones, sound, volume, audio, speakers, microphone, mic, headset, earphones"), "", "kcm_pulseaudio"},
    {"kde.bluetooth", "system", QT_TRANSLATE_NOOP("SettingsCatalog", "KDE System Settings"), QT_TRANSLATE_NOOP("SettingsCatalog", "Bluetooth"), QT_TRANSLATE_NOOP("SettingsCatalog", "Opens System Settings → Bluetooth."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "bluetooth, wireless headphones, pairing, devices"), "", "kcm_bluetooth"},
    {"kde.network", "system", QT_TRANSLATE_NOOP("SettingsCatalog", "KDE System Settings"), QT_TRANSLATE_NOOP("SettingsCatalog", "Network and Wi-Fi"), QT_TRANSLATE_NOOP("SettingsCatalog", "Opens System Settings → Wi-Fi & Networking."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "wifi, wi-fi, network, internet, vpn, ethernet, connection"), "", "kcm_networkmanagement"},
    {"kde.displays", "system", QT_TRANSLATE_NOOP("SettingsCatalog", "KDE System Settings"), QT_TRANSLATE_NOOP("SettingsCatalog", "Displays and scale"), QT_TRANSLATE_NOOP("SettingsCatalog", "Opens System Settings → Display Configuration."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "monitor, screen, display, resolution, scale, scaling, refresh rate"), "", "kcm_kscreen"},
    {"kde.notifications", "conversation", QT_TRANSLATE_NOOP("SettingsCatalog", "Reminders"), QT_TRANSLATE_NOOP("SettingsCatalog", "Notifications and Do Not Disturb"), QT_TRANSLATE_NOOP("SettingsCatalog", "Opens System Settings → Notifications."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "notifications, do not disturb, popups, alerts"), "", "kcm_notifications"},
    {"kde.shortcuts", "conversation", QT_TRANSLATE_NOOP("SettingsCatalog", "Keyboard shortcuts"), QT_TRANSLATE_NOOP("SettingsCatalog", "All keyboard shortcuts"), QT_TRANSLATE_NOOP("SettingsCatalog", "Opens System Settings → Shortcuts."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "shortcuts, hotkeys, keys, buttons, combination, keyboard"), "", "kcm_keys"},
    {"kde.power", "system", QT_TRANSLATE_NOOP("SettingsCatalog", "KDE System Settings"), QT_TRANSLATE_NOOP("SettingsCatalog", "Power and battery"), QT_TRANSLATE_NOOP("SettingsCatalog", "Opens System Settings → Power Management."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "battery, power, sleep, energy, suspend, laptop"), "", "kcm_powerdevilprofilesconfig"},
    {"kde.appearance", "system", QT_TRANSLATE_NOOP("SettingsCatalog", "KDE System Settings"), QT_TRANSLATE_NOOP("SettingsCatalog", "Global theme and colours of KDE"), QT_TRANSLATE_NOOP("SettingsCatalog", "Opens System Settings → Global Theme."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "global theme, colours, colors, dark, light, appearance, kde look"), "", "kcm_lookandfeel"},
    {"kde.autostart", "system", QT_TRANSLATE_NOOP("SettingsCatalog", "KDE System Settings"), QT_TRANSLATE_NOOP("SettingsCatalog", "Autostart of other applications"), QT_TRANSLATE_NOOP("SettingsCatalog", "Opens System Settings → Autostart."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "autostart, startup applications, login"), "", "kcm_autostart"},
    {"kde.region", "system", QT_TRANSLATE_NOOP("SettingsCatalog", "KDE System Settings"), QT_TRANSLATE_NOOP("SettingsCatalog", "Region and language of KDE"), QT_TRANSLATE_NOOP("SettingsCatalog", "Opens System Settings → Region & Language."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "locale, region, date format, time format, language, units"), "", "kcm_regionandlang"},
    {"kde.keyboard", "system", QT_TRANSLATE_NOOP("SettingsCatalog", "KDE System Settings"), QT_TRANSLATE_NOOP("SettingsCatalog", "Keyboard layouts"), QT_TRANSLATE_NOOP("SettingsCatalog", "Opens System Settings → Keyboard."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "keyboard, layout, input language, switch layout"), "", "kcm_keyboard"},
    {"system.reset", "system", QT_TRANSLATE_NOOP("SettingsCatalog", "Recovery"), QT_TRANSLATE_NOOP("SettingsCatalog", "Reset Jarvis settings"), QT_TRANSLATE_NOOP("SettingsCatalog", "One page or all of them; memory, history and keys stay."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "reset, defaults, factory settings, restore defaults"), "", ""},
    {"system.restore", "system", QT_TRANSLATE_NOOP("SettingsCatalog", "Recovery"), QT_TRANSLATE_NOOP("SettingsCatalog", "Back to the standard KDE desktop"), QT_TRANSLATE_NOOP("SettingsCatalog", "Puts Plasma's layout back as it was before JARVIS DESKTOP."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "restore, standard desktop, kde, undo, rollback, plasma"), "", ""},
    {"system.files", "system", QT_TRANSLATE_NOOP("SettingsCatalog", "Recovery"), QT_TRANSLATE_NOOP("SettingsCatalog", "Settings files and going back to an older version"), QT_TRANSLATE_NOOP("SettingsCatalog", "Where the settings are, the backup, and what to do before installing an older Jarvis."),
     QT_TRANSLATE_NOOP("SettingsCatalog", "files, config, backup, downgrade, rollback, older version"), "", ""},
};

const struct
{
    const char *id;
    const char *title;
} kPages[] = {
    {"desktop", QT_TRANSLATE_NOOP("SettingsCatalog", "Desktop")},
    {"conversation", QT_TRANSLATE_NOOP("SettingsCatalog", "Talking to Jarvis")},
    {"intelligence", QT_TRANSLATE_NOOP("SettingsCatalog", "Intelligence and connection")},
    {"privacy", QT_TRANSLATE_NOOP("SettingsCatalog", "Memory and privacy")},
    {"actions", QT_TRANSLATE_NOOP("SettingsCatalog", "Actions and permissions")},
    {"skills", QT_TRANSLATE_NOOP("SettingsCatalog", "Skills")},
    {"system", QT_TRANSLATE_NOOP("SettingsCatalog", "System and recovery")},
};

QString tr(const char *text)
{
    return QCoreApplication::translate("SettingsCatalog", text);
}

QVariantMap toMap(const Entry &e)
{
    return {{u"id"_s, QString::fromLatin1(e.id)},
            {u"page"_s, QString::fromLatin1(e.page)},
            {u"pageTitle"_s, pageTitle(QString::fromLatin1(e.page))},
            {u"group"_s, tr(e.group)},
            {u"title"_s, tr(e.title)},
            {u"description"_s, tr(e.description)},
            {u"configKey"_s, QString::fromLatin1(e.configKey)},
            {u"kcm"_s, QString::fromLatin1(e.kcm)}};
}

QStringList words(const QString &text)
{
    return normalize(text).split(u' ', Qt::SkipEmptyParts);
}

// A query word matches a text word as its start; with five letters or more the
// ending may differ by two ("наушниках" finds "наушники", "keys" finds "key").
bool matches(const QString &query, const QString &word)
{
    if (word.startsWith(query))
        return true;
    if (query.size() >= 5 && word.startsWith(query.left(query.size() - 2)))
        return true;
    return word.size() >= 3 && query.size() >= 4 && query.startsWith(word) && query.size() - word.size() <= 2;
}

int fieldScore(const QString &token, const QStringList &field)
{
    for (const QString &w : field)
        if (matches(token, w))
            return w == token ? 2 : 1;
    return 0;
}

} // namespace

QString normalize(const QString &text)
{
    QString s = text.toLower();
    s.replace(QChar(0x0451), QChar(0x0435)); // ё → е
    static const QRegularExpression other(u"[^\\p{L}\\p{N}]+"_s);
    s.replace(other, u" "_s);
    return s.simplified();
}

QStringList pages()
{
    QStringList out;
    for (const auto &p : kPages)
        out << QString::fromLatin1(p.id);
    return out;
}

QString pageTitle(const QString &page)
{
    for (const auto &p : kPages)
        if (page == QLatin1String(p.id))
            return tr(p.title);
    return {};
}

QVariantList entries()
{
    QVariantList out;
    for (const Entry &e : kEntries)
        out << toMap(e);
    return out;
}

QVariantMap entry(const QString &id)
{
    for (const Entry &e : kEntries)
        if (id == QLatin1String(e.id))
            return toMap(e);
    return {};
}

QVariantList search(const QString &query, int limit)
{
    const QStringList tokens = words(query);
    if (tokens.isEmpty())
        return {};
    struct Hit
    {
        int score;
        int order;
    };
    QList<Hit> hits;
    int order = 0;
    for (const Entry &e : kEntries) {
        // Translated and English text both count: an English word finds a Russian page.
        const QStringList title = words(tr(e.title) + u' ' + QString::fromUtf8(e.title));
        const QStringList synonyms = words(tr(e.synonyms) + u' ' + QString::fromUtf8(e.synonyms) + u' ' + tr(e.group));
        const QStringList description = words(tr(e.description) + u' ' + pageTitle(QString::fromLatin1(e.page)));
        int score = 0;
        bool all = true;
        for (const QString &t : tokens) {
            const int best = std::max({fieldScore(t, title) * 6, fieldScore(t, synonyms) * 4, fieldScore(t, description) * 2});
            if (best == 0) {
                all = false;
                break;
            }
            score += best;
        }
        if (all) {
            if (normalize(tr(e.title)).startsWith(normalize(query)))
                score += 5;
            hits.append({score, order});
        }
        ++order;
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) { return a.score > b.score; });
    QVariantList out;
    for (const Hit &h : hits) {
        if (out.size() >= limit)
            break;
        out << toMap(kEntries[h.order]);
    }
    return out;
}

QStringList titlesForKeys(const QStringList &keys)
{
    QStringList out;
    for (const QString &key : keys) {
        QString title;
        for (const Entry &e : kEntries) {
            const QString k = QString::fromLatin1(e.configKey);
            // Keys that share a row: the desktop's colours and the core's place, the reminder hours, limits.
            static const QHash<QString, QString> shared{{u"desktop.secondary"_s, u"desktop.primary"_s},
                                                        {u"desktop.corePosition"_s, u"desktop.coreSize"_s},
                                                        {u"afternoon_hour"_s, u"morning_hour"_s},
                                                        {u"evening_hour"_s, u"morning_hour"_s},
                                                        {u"suggestions_per_day"_s, u"suggestion_cooldown_minutes"_s}};
            if (!k.isEmpty() && k == shared.value(key, key)) {
                title = tr(e.title);
                break;
            }
        }
        if (!title.isEmpty() && !out.contains(title))
            out << title;
    }
    return out;
}

} // namespace jarvis::settings
