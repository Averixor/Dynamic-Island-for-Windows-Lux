#pragma once

#include "settings_schema.h"
#include "settings_store.h"

#include <windows.h>

#include <string>

// UI strings for the settings dialog (standalone). Independent of .wh.cpp Loc().
namespace SettingsUiLoc {

enum class Lang { En, Uk, Ru };

enum class SettingsCard {
  ThemeLook,
  ThemeAccent,
  ThemeSurface,
  ThemeBackdrop,
  ThemeAura,
  Media,
  MediaClock,
  Volume,
  Clipboard,
  Battery,
  Progress,
  CapsLock,
  Weather,
  Calendar,
  Monitor,
  Bluetooth,
  DoNotDisturb,
  FocusTimer,
  Lyrics,
  QuickLookup,
  FileTray,
  GameOverlay,
  Appearance,
  Behavior,
  Animations,
  Indicators,
  Shortcuts,
  General,
};

inline Lang ResolveLang() {
  std::wstring code = SettingsGetString(L"General.Language", L"uk");
  if (_wcsicmp(code.c_str(), L"auto") == 0) {
    const LANGID id = GetUserDefaultUILanguage();
    const WORD primary = PRIMARYLANGID(id);
    if (primary == LANG_UKRAINIAN) {
      return Lang::Uk;
    }
    if (primary == LANG_RUSSIAN) {
      return Lang::Ru;
    }
    return Lang::En;
  }
  if (_wcsicmp(code.c_str(), L"uk") == 0) {
    return Lang::Uk;
  }
  if (_wcsicmp(code.c_str(), L"ru") == 0) {
    return Lang::Ru;
  }
  return Lang::En;
}

inline const wchar_t* Pick(Lang lang, const wchar_t* en, const wchar_t* uk, const wchar_t* ru) {
  switch (lang) {
  case Lang::Uk:
    return uk;
  case Lang::Ru:
    return ru;
  case Lang::En:
  default:
    return en;
  }
}

inline const wchar_t* WindowTitle(Lang lang) {
  return Pick(lang, L"Dynamic Island Lux — Settings", L"Dynamic Island Lux — Налаштування",
              L"Dynamic Island Lux — Настройки");
}

inline const wchar_t* FilterCue(Lang lang) {
  return Pick(lang, L"Filter settings…", L"Фільтр налаштувань…", L"Фильтр настроек…");
}

inline const wchar_t* Ok(Lang lang) {
  return Pick(lang, L"OK", L"OK", L"OK");
}

inline const wchar_t* Cancel(Lang lang) {
  return Pick(lang, L"Cancel", L"Скасувати", L"Отменить");
}

inline const wchar_t* Apply(Lang lang) {
  return Pick(lang, L"Apply", L"Застосувати", L"Применить");
}

inline const wchar_t* ResetDefaults(Lang lang) {
  return Pick(lang, L"Reset to defaults", L"Скинути до заводських", L"Сбросить к заводским");
}

inline const wchar_t* ResetDefaultsConfirm(Lang lang) {
  return Pick(lang,
              L"Reset all settings to factory defaults?\nDrag position, pin, and hidden state will also be cleared.",
              L"Скинути всі налаштування до заводських?\nЗсув, закріплення і прихований стан також буде скинуто.",
              L"Сбросить все настройки к заводским?\nСмещение, закрепление и скрытое состояние тоже будут сброшены.");
}

inline const wchar_t* AutostartWriteFailed(Lang lang) {
  return Pick(lang, L"Could not update the Windows startup entry.",
              L"Не вдалося оновити запис автозапуску Windows.",
              L"Не удалось обновить запись автозапуска Windows.");
}

inline const wchar_t* HotkeyCollision(Lang lang) {
  return Pick(lang, L"Lookup and Hide/Show use the same shortcut. Choose a different key or modifiers.",
              L"Пошук і сховати/показати мають однакове поєднання. Оберіть іншу клавішу або модифікатори.",
              L"Поиск и скрыть/показать используют одно сочетание. Выберите другую клавишу или модификаторы.");
}

inline const wchar_t* ResetDefaultsTitle(Lang lang) {
  return Pick(lang, L"Reset settings", L"Скидання налаштувань", L"Сброс настроек");
}

inline const wchar_t* Autostart(Lang lang) {
  return Pick(lang, L"Launch with system", L"Запускати разом із системою",
              L"Запускать вместе с системой");
}

inline const wchar_t* TraySettings(Lang lang) {
  return Pick(lang, L"Settings", L"Налаштування", L"Настройки");
}

inline const wchar_t* TrayShow(Lang lang) {
  return Pick(lang, L"Show / hide island", L"Показати / сховати острів",
              L"Показать / скрыть остров");
}

inline const wchar_t* TrayReload(Lang lang) {
  return Pick(lang, L"Restart", L"Перезапуск", L"Перезапуск");
}

inline const wchar_t* TrayExit(Lang lang) {
  return Pick(lang, L"Exit", L"Вихід", L"Выход");
}

inline const wchar_t* TrayTip(Lang lang) {
  return Pick(lang, L"Dynamic Island for Windows Lux", L"Dynamic Island for Windows Lux",
              L"Dynamic Island for Windows Lux");
}

inline const wchar_t* GroupTitle(Lang lang, SettingGroup g) {
  switch (g) {
  case SettingGroup::General:
    return Pick(lang, L"General", L"Загальне", L"Общие");
  case SettingGroup::Appearance:
    return Pick(lang, L"Appearance", L"Вигляд", L"Внешний вид");
  case SettingGroup::Themes:
    return Pick(lang, L"Themes", L"Теми", L"Темы");
  case SettingGroup::Animations:
    return Pick(lang, L"Animations", L"Анімації", L"Анимации");
  case SettingGroup::Behavior:
    return Pick(lang, L"Behavior", L"Поведінка", L"Поведение");
  case SettingGroup::Shortcuts:
    return Pick(lang, L"Shortcuts", L"Гарячі клавіші", L"Горячие клавиши");
  case SettingGroup::Modules:
    return Pick(lang, L"Modules", L"Модулі", L"Модули");
  case SettingGroup::Indicators:
    return Pick(lang, L"Indicators", L"Індикатори", L"Индикаторы");
  case SettingGroup::Runtime:
    return Pick(lang, L"Runtime", L"Службове", L"Служебное");
  }
  return Pick(lang, L"Other", L"Інше", L"Прочее");
}

inline const wchar_t* SettingLabel(Lang lang, const wchar_t* key) {
  if (wcscmp(key, L"Appearance.Position") == 0) {
    return Pick(lang, L"Position", L"Позиція", L"Позиция");
  }
  if (wcscmp(key, L"Appearance.SizeScale") == 0) {
    return Pick(lang, L"Size scale", L"Масштаб розміру", L"Масштаб размера");
  }
  if (wcscmp(key, L"Appearance.AutoDpiScale") == 0) {
    return Pick(lang, L"Auto DPI scale", L"Автомасштаб DPI", L"Автомасштаб DPI");
  }
  if (wcscmp(key, L"Appearance.OffsetX") == 0) {
    return Pick(lang, L"Offset X", L"Зсув X", L"Смещение X");
  }
  if (wcscmp(key, L"Appearance.OffsetY") == 0) {
    return Pick(lang, L"Offset Y", L"Зсув Y", L"Смещение Y");
  }
  if (wcscmp(key, L"Appearance.SeparateExpandedOffsetY") == 0) {
    return Pick(lang, L"Separate expanded Y offset", L"Окремий Y для розгорнутого",
                L"Отдельный Y для раскрытого");
  }
  if (wcscmp(key, L"Appearance.OffsetYExpanded") == 0) {
    return Pick(lang, L"Expanded offset Y", L"Зсув Y (розгорнутий)", L"Смещение Y (раскрытый)");
  }
  if (wcscmp(key, L"Appearance.ProgressStyle") == 0) {
    return Pick(lang, L"Progress style", L"Стиль прогресу", L"Стиль прогресса");
  }
  if (wcscmp(key, L"Appearance.BorderMergedMode") == 0) {
    return Pick(lang, L"Merge with screen edge", L"Злити з краєм екрана", L"Слить с краем экрана");
  }
  if (wcscmp(key, L"Animations.ExpandedMediaTransitions") == 0) {
    return Pick(lang, L"Expanded media transitions", L"Анімації розгорнутого медіа",
                L"Анимации раскрытого медиа");
  }
  if (wcscmp(key, L"Animations.PillCoverSpin") == 0) {
    return Pick(lang, L"Cover spin on pill", L"Обертання обкладинки", L"Вращение обложки");
  }
  if (wcscmp(key, L"Animations.TargetFPS") == 0) {
    return Pick(lang, L"Target FPS", L"Цільовий FPS", L"Целевой FPS");
  }
  if (wcscmp(key, L"Animations.AnimationStyle") == 0) {
    return Pick(lang, L"Animation style", L"Стиль анімації", L"Стиль анимации");
  }
  if (wcscmp(key, L"Animations.AnimationSpeed") == 0) {
    return Pick(lang, L"Animation speed", L"Швидкість анімації", L"Скорость анимации");
  }
  if (wcscmp(key, L"Behavior.AlwaysOnTop") == 0) {
    return Pick(lang, L"Always on top", L"Поверх усіх вікон", L"Поверх всех окон");
  }
  if (wcscmp(key, L"Behavior.AutoHideIdleSeconds") == 0) {
    return Pick(lang, L"Auto-hide after idle (seconds; 0 = never, -1 = hover/events)",
                L"Автоприховування (секунди; 0 = ніколи, -1 = наведення/події)",
                L"Автоскрытие (секунды; 0 = никогда, -1 = наведение/события)");
  }
  if (wcscmp(key, L"Behavior.ExpandMode") == 0) {
    return Pick(lang, L"Open on", L"Розкриття", L"Раскрытие");
  }
  if (wcscmp(key, L"Behavior.UnhideOnHover") == 0) {
    return Pick(lang, L"Unhide on hover", L"Показати при наведенні", L"Показать при наведении");
  }
  if (wcscmp(key, L"Behavior.AllowDragMove") == 0) {
    return Pick(lang, L"Allow drag move", L"Перетягування мишкою", L"Перетаскивание мышью");
  }
  if (wcscmp(key, L"Behavior.AutoHideFullscreen") == 0) {
    return Pick(lang, L"Hide in fullscreen", L"Ховати в повноекранних", L"Скрывать в полноэкранных");
  }
  if (wcscmp(key, L"Behavior.ClickThroughModifier") == 0) {
    return Pick(lang, L"Click-through modifier", L"Модифікатор наскрізних кліків",
                L"Модификатор сквозных кликов");
  }
  if (wcscmp(key, L"Behavior.WheelStep") == 0) {
    return Pick(lang, L"Wheel step (%)", L"Крок колеса (%)", L"Шаг колеса (%)");
  }
  if (wcscmp(key, L"Behavior.AppBlacklist") == 0) {
    return Pick(lang, L"App blacklist (exe names)", L"Чорний список додатків (exe)",
                L"Чёрный список приложений (exe)");
  }
  if (wcscmp(key, L"Behavior.BlacklistMode") == 0) {
    return Pick(lang, L"Blacklist mode", L"Режим чорного списку", L"Режим чёрного списка");
  }
  if (wcscmp(key, L"Appearance.TargetMonitor") == 0) {
    return Pick(lang, L"Target monitor", L"Цільовий монітор", L"Целевой монитор");
  }
  if (wcscmp(key, L"Appearance.SpectrumStyle") == 0) {
    return Pick(lang, L"Spectrum style", L"Стиль спектра", L"Стиль спектра");
  }
  if (wcscmp(key, L"Appearance.BackdropMaterial") == 0) {
    return Pick(lang, L"Backdrop material", L"Матеріал підложки", L"Материал подложки");
  }
  if (wcscmp(key, L"Appearance.UseDirectComposition") == 0) {
    return Pick(lang, L"DirectComposition rendering", L"Рендер DirectComposition",
                L"Рендер DirectComposition");
  }
  if (wcscmp(key, L"Appearance.ShapeStyle") == 0) {
    return Pick(lang, L"Island shape", L"Форма острова", L"Форма острова");
  }
  if (wcscmp(key, L"Themes.ThemePreset") == 0) {
    return Pick(lang, L"Theme", L"Тема", L"Тема");
  }
  if (wcscmp(key, L"Themes.AccentColorMode") == 0) {
    return Pick(lang, L"Accent color mode", L"Режим акцентного кольору",
                L"Режим акцентного цвета");
  }
  if (wcscmp(key, L"Themes.CustomAccentHex") == 0) {
    return Pick(lang, L"Custom accent (#hex)", L"Власний акцент (#hex)", L"Свой акцент (#hex)");
  }
  if (wcscmp(key, L"Themes.AccentBloom") == 0) {
    return Pick(lang, L"Accent bloom", L"Свічення акценту", L"Свечение акцента");
  }
  if (wcscmp(key, L"Themes.TintIntensity") == 0) {
    return Pick(lang, L"Tint intensity", L"Інтенсивність тону", L"Интенсивность тона");
  }
  if (wcscmp(key, L"Themes.MaterialDepth") == 0) {
    return Pick(lang, L"Material depth", L"Глибина матеріалу", L"Глубина материала");
  }
  if (wcscmp(key, L"Themes.DropShadow") == 0) {
    return Pick(lang, L"Drop shadow", L"Тінь", L"Тень");
  }
  if (wcscmp(key, L"Themes.ClockAccentGlow") == 0) {
    return Pick(lang, L"Clock accent glow", L"Свічення годинника", L"Свечение часов");
  }
  if (wcscmp(key, L"Themes.ContourBorderMode") == 0) {
    return Pick(lang, L"Border mode", L"Режим контуру", L"Режим контура");
  }
  if (wcscmp(key, L"Themes.ContourBorderHex") == 0) {
    return Pick(lang, L"Border color (#hex)", L"Колір контуру (#hex)", L"Цвет контура (#hex)");
  }
  if (wcscmp(key, L"Themes.PillBgColor") == 0) {
    return Pick(lang, L"Custom pill background", L"Власний фон пігулки", L"Свой фон таблетки");
  }
  if (wcscmp(key, L"Themes.TextPrimaryColor") == 0) {
    return Pick(lang, L"Custom primary text", L"Власний основний текст", L"Свой основной текст");
  }
  if (wcscmp(key, L"Themes.TextSecondaryColor") == 0) {
    return Pick(lang, L"Custom secondary text", L"Власний другорядний текст",
                L"Свой второстепенный текст");
  }
  if (wcscmp(key, L"Themes.FontFamily") == 0) {
    return Pick(lang, L"Font family", L"Шрифт", L"Шрифт");
  }
  if (wcscmp(key, L"Themes.CalendarAccent") == 0) {
    return Pick(lang, L"Calendar accent", L"Акцент календаря", L"Акцент календаря");
  }
  if (wcscmp(key, L"Modules.CollapsedLyrics") == 0) {
    return Pick(lang, L"Lyrics on collapsed island", L"Текст пісні на згорнутому острові",
                L"Текст песни на свёрнутом острове");
  }
  if (wcscmp(key, L"Modules.LyricsPinnedFirst") == 0) {
    return Pick(lang, L"Lyrics as first tab", L"Текст пісні першою вкладкою",
                L"Текст песни первой вкладкой");
  }
  if (wcscmp(key, L"Modules.MediaTabLyrics") == 0) {
    return Pick(lang, L"Lyrics in media tab", L"Текст пісні у вкладці медіа",
                L"Текст песни во вкладке медиа");
  }
  if (wcscmp(key, L"Modules.TimerContourProgress") == 0) {
    return Pick(lang, L"Timer contour progress", L"Контур прогресу таймера",
                L"Контур прогресса таймера");
  }
  if (wcscmp(key, L"Indicators.PrivacyDots") == 0) {
    return Pick(lang, L"Privacy dots", L"Індикатори приватності", L"Индикаторы приватности");
  }
  if (wcscmp(key, L"Indicators.PrivacyDotsMic") == 0) {
    return Pick(lang, L"Microphone dot", L"Точка мікрофона", L"Точка микрофона");
  }
  if (wcscmp(key, L"Indicators.PrivacyDotsCam") == 0) {
    return Pick(lang, L"Camera dot", L"Точка камери", L"Точка камеры");
  }
  if (wcscmp(key, L"Modules.LyricsVisualizer") == 0) {
    return Pick(lang, L"Lyrics visualizer", L"Візуалізатор тексту пісні", L"Визуализатор текста песни");
  }
  if (wcscmp(key, L"Themes.BackdropMaterial") == 0) {
    return Pick(lang, L"Backdrop material", L"Матеріал фону", L"Материал фона");
  }
  if (wcscmp(key, L"Themes.AuraStyle") == 0) {
    return Pick(lang, L"Aura", L"Ореол", L"Ореол");
  }
  if (wcscmp(key, L"Themes.AuraIntensity") == 0) {
    return Pick(lang, L"Aura intensity", L"Сила ореолу", L"Сила ореола");
  }
  if (wcscmp(key, L"Themes.AdaptiveTheme") == 0) {
    return Pick(lang, L"Adaptive theme", L"Адаптивна тема", L"Адаптивная тема");
  }
  if (wcscmp(key, L"Modules.Media") == 0) {
    return Pick(lang, L"Media player", L"Медіаплеєр", L"Медиаплеер");
  }
  if (wcscmp(key, L"Modules.MediaAutoExpand") == 0) {
    return Pick(lang, L"Auto-expand on track change", L"Авторозгортання при зміні треку",
                L"Автораскрытие при смене трека");
  }
  if (wcscmp(key, L"Modules.MediaLikeBridge") == 0) {
    return Pick(lang, L"YouTube / YouTube Music like bridge",
                L"Міст лайків YouTube і YouTube Music",
                L"Мост лайков YouTube и YouTube Music");
  }
  if (wcscmp(key, L"Modules.MediaLikeBridgePort") == 0) {
    return Pick(lang, L"Like bridge loopback port", L"Порт моста лайків (127.0.0.1)",
                L"Порт моста лайков (127.0.0.1)");
  }
  if (wcscmp(key, L"Modules.MediaExpandBlocklist") == 0) {
    return Pick(lang, L"Media expand blocklist", L"Чорний список авторозгортання медіа",
                L"Чёрный список автораскрытия медиа");
  }
  if (wcscmp(key, L"Modules.Volume") == 0) {
    return Pick(lang, L"Volume", L"Гучність", L"Громкость");
  }
  if (wcscmp(key, L"Modules.StatusCountdownProgress") == 0) {
    return Pick(lang, L"Status countdown bar", L"Смужка зворотного відліку",
                L"Полоса обратного отсчёта");
  }
  if (wcscmp(key, L"Modules.ShowMetricText") == 0) {
    return Pick(lang, L"Show metric labels", L"Підписи метрик", L"Подписи метрик");
  }
  if (wcscmp(key, L"Modules.BluetoothShowBattery") == 0) {
    return Pick(lang, L"Bluetooth battery", L"Заряд Bluetooth", L"Заряд Bluetooth");
  }
  if (wcscmp(key, L"Modules.NotificationRespectDnD") == 0) {
    return Pick(lang, L"Respect Do Not Disturb", L"Поважати «Не турбувати»",
                L"Учитывать «Не беспокоить»");
  }
  if (wcscmp(key, L"Modules.Notifications") == 0) {
    return Pick(lang, L"Windows notifications", L"Сповіщення Windows", L"Уведомления Windows");
  }
  if (wcscmp(key, L"Modules.MessengerApps") == 0) {
    return Pick(lang, L"Messenger apps (comma-separated)", L"Месенджери (через кому)",
                L"Мессенджеры (через запятую)");
  }
  if (wcscmp(key, L"Modules.WeatherCity") == 0) {
    return Pick(lang, L"Weather city (blank uses your IP)",
                L"Місто для погоди (порожнє — за IP)",
                L"Город для погоды (пусто — по IP)");
  }
  if (wcscmp(key, L"Modules.FirstDayOfWeek") == 0) {
    return Pick(lang, L"First day of week", L"Перший день тижня", L"Первый день недели");
  }
  if (wcscmp(key, L"Modules.Lyrics") == 0) {
    return Pick(lang, L"Lyrics", L"Текст пісні", L"Текст песни");
  }
  if (wcscmp(key, L"Modules.KaraokeLyrics") == 0) {
    return Pick(lang, L"Karaoke lyrics", L"Караоке-текст", L"Караоке-текст");
  }
  if (wcscmp(key, L"Modules.KaraokeLetterGlow") == 0) {
    return Pick(lang, L"Karaoke letter glow", L"Підсвітка літер караоке",
                L"Подсветка букв караоке");
  }
  if (wcscmp(key, L"Modules.LyricsNeighborLinesVisible") == 0) {
    return Pick(lang, L"Show neighbor lyric lines", L"Сусідні рядки тексту",
                L"Соседние строки текста");
  }
  if (wcscmp(key, L"Modules.LyricsOffsetMs") == 0) {
    return Pick(lang, L"Lyrics offset (ms)", L"Зсув тексту (мс)", L"Смещение текста (мс)");
  }
  if (wcscmp(key, L"Modules.LyricsOffsetBluetoothMs") == 0) {
    return Pick(lang, L"Lyrics Bluetooth offset (ms)", L"Зсув тексту Bluetooth (мс)",
                L"Смещение текста Bluetooth (мс)");
  }
  if (wcscmp(key, L"Modules.LyricsCache") == 0) {
    return Pick(lang, L"Cache lyrics", L"Кеш тексту пісень", L"Кэш текста песен");
  }
  if (wcscmp(key, L"Modules.LyricsCacheMaxMB") == 0) {
    return Pick(lang, L"Lyrics cache max (MB)", L"Макс. кеш тексту (МБ)",
                L"Макс. кэш текста (МБ)");
  }
  if (wcscmp(key, L"Modules.Clipboard") == 0) {
    return Pick(lang, L"Clipboard", L"Буфер обміну", L"Буфер обмена");
  }
  if (wcscmp(key, L"Modules.Battery") == 0) {
    return Pick(lang, L"Battery", L"Батарея", L"Батарея");
  }
  if (wcscmp(key, L"Modules.Progress") == 0) {
    return Pick(lang, L"Progress", L"Прогрес", L"Прогресс");
  }
  if (wcscmp(key, L"Modules.CapsLock") == 0) {
    return Pick(lang, L"Caps/Num Lock", L"Caps/Num Lock", L"Caps/Num Lock");
  }
  if (wcscmp(key, L"Modules.Weather") == 0) {
    return Pick(lang, L"Weather", L"Погода", L"Погода");
  }
  if (wcscmp(key, L"Modules.WeatherFahrenheit") == 0) {
    return Pick(lang, L"Use Fahrenheit", L"Показувати у Фаренгейтах", L"Показывать в Фаренгейтах");
  }
  if (wcscmp(key, L"Modules.CalendarModule") == 0) {
    return Pick(lang, L"Calendar", L"Календар", L"Календарь");
  }
  if (wcscmp(key, L"Modules.HardwareMonitorModule") == 0) {
    return Pick(lang, L"Hardware monitor", L"Монітор системи", L"Монитор системы");
  }
  if (wcscmp(key, L"Modules.BluetoothIndicator") == 0) {
    return Pick(lang, L"Bluetooth", L"Bluetooth", L"Bluetooth");
  }
  if (wcscmp(key, L"Modules.DoNotDisturbIndicator") == 0) {
    return Pick(lang, L"Do Not Disturb", L"Не турбувати", L"Не беспокоить");
  }
  if (wcscmp(key, L"Modules.TimerModule") == 0) {
    return Pick(lang, L"Focus timer", L"Таймер фокусу", L"Таймер фокуса");
  }
  if (wcscmp(key, L"Modules.MediaPillClock") == 0) {
    return Pick(lang, L"Clock beside media", L"Годинник біля плеєра", L"Часы у плеера");
  }
  if (wcscmp(key, L"Modules.MediaPillDate") == 0) {
    return Pick(lang, L"Date beside media", L"Дата біля плеєра", L"Дата у плеера");
  }
  if (wcscmp(key, L"Modules.ClockFormat") == 0) {
    return Pick(lang, L"Clock format", L"Формат годинника", L"Формат часов");
  }
  if (wcscmp(key, L"Modules.DateFormat") == 0) {
    return Pick(lang, L"Date format", L"Формат дати", L"Формат даты");
  }
  if (wcscmp(key, L"Modules.ShowSeconds") == 0) {
    return Pick(lang, L"Always show seconds", L"Завжди з секундами", L"Всегда с секундами");
  }
  if (wcscmp(key, L"Modules.DateFirst") == 0) {
    return Pick(lang, L"Date first", L"Дата першою", L"Дата первой");
  }
  if (wcscmp(key, L"Themes.BackdropTint") == 0) {
    return Pick(lang, L"Backdrop tint", L"Відтінок фону", L"Оттенок фона");
  }
  if (wcscmp(key, L"Themes.BackdropFillOpacity") == 0) {
    return Pick(lang, L"Backdrop opacity", L"Непрозорість фону", L"Непрозрачность фона");
  }
  if (wcscmp(key, L"Themes.PillOpacity") == 0) {
    return Pick(lang, L"Island opacity (%)", L"Непрозорість острова (%)", L"Непрозрачность острова (%)");
  }
  if (wcscmp(key, L"Themes.TextScale") == 0) {
    return Pick(lang, L"Text size (%)", L"Розмір тексту (%)", L"Размер текста (%)");
  }
  if (wcscmp(key, L"General.Language") == 0) {
    return Pick(lang, L"Language", L"Мова", L"Язык");
  }
  if (wcscmp(key, L"General.LaunchAtStartup") == 0) {
    return Autostart(lang);
  }
  if (wcscmp(key, L"Modules.QuickLookup") == 0) {
    return Pick(lang, L"Quick Lookup", L"Швидкий пошук", L"Быстрый поиск");
  }
  if (wcscmp(key, L"Shortcuts.LookupModifiers") == 0) {
    return Pick(lang, L"Lookup modifiers", L"Модифікатори пошуку", L"Модификаторы поиска");
  }
  if (wcscmp(key, L"Shortcuts.LookupKey") == 0) {
    return Pick(lang, L"Lookup key", L"Клавіша пошуку", L"Клавиша поиска");
  }
  if (wcscmp(key, L"Shortcuts.HideShowHotkeyEnabled") == 0) {
    return Pick(lang, L"Show/hide shortcut", L"Гаряча клавіша показу/приховування",
                L"Горячая клавиша показа/скрытия");
  }
  if (wcscmp(key, L"Shortcuts.HideShowModifiers") == 0) {
    return Pick(lang, L"Show/hide modifiers", L"Модифікатори показу/приховування",
                L"Модификаторы показа/скрытия");
  }
  if (wcscmp(key, L"Shortcuts.HideShowKey") == 0) {
    return Pick(lang, L"Show/hide key", L"Клавіша показу/приховування",
                L"Клавиша показа/скрытия");
  }
  if (wcscmp(key, L"Modules.FileTrayModule") == 0) {
    return Pick(lang, L"File tray", L"Файловий лоток", L"Файловый лоток");
  }
  if (wcscmp(key, L"Modules.FileTrayMaxItems") == 0) {
    return Pick(lang, L"File tray max items", L"Макс. файлів у лотку", L"Макс. файлов в лотке");
  }
  if (wcscmp(key, L"Modules.FileTrayPersist") == 0) {
    return Pick(lang, L"Persist file tray", L"Зберігати лоток", L"Сохранять лоток");
  }
  if (wcscmp(key, L"Modules.GameOverlay") == 0) {
    return Pick(lang, L"Game overlay", L"Ігровий оверлей", L"Игровой оверлей");
  }
  if (wcscmp(key, L"Modules.GameOverlayShowFps") == 0) {
    return Pick(lang, L"Show FPS", L"Показувати FPS", L"Показывать FPS");
  }
  if (wcscmp(key, L"Modules.GameOverlayShowCpu") == 0) {
    return Pick(lang, L"Show CPU", L"Показувати CPU", L"Показывать CPU");
  }
  if (wcscmp(key, L"Modules.GameOverlayShowGpu") == 0) {
    return Pick(lang, L"Show GPU", L"Показувати GPU", L"Показывать GPU");
  }
  if (wcscmp(key, L"Modules.GameOverlayShowRam") == 0) {
    return Pick(lang, L"Show RAM", L"Показувати RAM", L"Показывать RAM");
  }
  if (wcscmp(key, L"Modules.GameOverlayShowDisk") == 0) {
    return Pick(lang, L"Show disk", L"Показувати диск", L"Показывать диск");
  }
  if (wcscmp(key, L"Modules.GameOverlayCompact") == 0) {
    return Pick(lang, L"Compact overlay", L"Компактний оверлей", L"Компактный оверлей");
  }
  if (wcscmp(key, L"app.version") == 0) {
    return Pick(lang, L"App version", L"Версія додатка", L"Версия приложения");
  }
  return key;
}

inline const wchar_t* BackdropLabel(Lang lang, const wchar_t* value) {
  if (_wcsicmp(value, L"none") == 0) {
    return Pick(lang, L"None", L"Без ефекту", L"Без эффекта");
  }
  if (_wcsicmp(value, L"blur") == 0) {
    return Pick(lang, L"Blur", L"Розмиття", L"Размытие");
  }
  if (_wcsicmp(value, L"acrylic") == 0) {
    return Pick(lang, L"Acrylic", L"Acrylic", L"Acrylic");
  }
  if (_wcsicmp(value, L"mica") == 0) {
    return Pick(lang, L"Mica", L"Mica", L"Mica");
  }
  if (_wcsicmp(value, L"solid") == 0) {
    return Pick(lang, L"Solid", L"Суцільний", L"Сплошной");
  }
  if (_wcsicmp(value, L"auto") == 0) {
    return Pick(lang, L"Auto (Themes)", L"Авто (Themes)", L"Авто (Themes)");
  }
  return value;
}

inline const wchar_t* AuraLabel(Lang lang, const wchar_t* value) {
  if (_wcsicmp(value, L"off") == 0) {
    return Pick(lang, L"Off", L"Вимкнено", L"Выключено");
  }
  if (_wcsicmp(value, L"glow") == 0) {
    return Pick(lang, L"Glow", L"Свічення", L"Свечение");
  }
  if (_wcsicmp(value, L"equalizer") == 0) {
    return Pick(lang, L"Equalizer", L"Еквалайзер", L"Эквалайзер");
  }
  if (_wcsicmp(value, L"rays") == 0) {
    return Pick(lang, L"Rays", L"Промені", L"Лучи");
  }
  if (_wcsicmp(value, L"pulse") == 0) {
    return Pick(lang, L"Rings", L"Кільця", L"Кольца");
  }
  if (_wcsicmp(value, L"orbit") == 0) {
    return Pick(lang, L"Orbit", L"Орбіта", L"Орбита");
  }
  return value;
}

inline const wchar_t* TargetMonitorLabel(Lang lang, const wchar_t* value) {
  if (_wcsicmp(value, L"primary") == 0) {
    return Pick(lang, L"Primary", L"Основний", L"Основной");
  }
  if (_wcsicmp(value, L"cursor") == 0) {
    return Pick(lang, L"Cursor monitor", L"Монітор курсора", L"Монитор курсора");
  }
  if (_wcsicmp(value, L"follow") == 0) {
    return Pick(lang, L"Follow active window", L"За активним вікном", L"За активным окном");
  }
  if (_wcsicmp(value, L"1") == 0) {
    return Pick(lang, L"Monitor 1", L"Монітор 1", L"Монитор 1");
  }
  if (_wcsicmp(value, L"2") == 0) {
    return Pick(lang, L"Monitor 2", L"Монітор 2", L"Монитор 2");
  }
  if (_wcsicmp(value, L"3") == 0) {
    return Pick(lang, L"Monitor 3", L"Монітор 3", L"Монитор 3");
  }
  return value;
}

inline const wchar_t* BlacklistModeLabel(Lang lang, const wchar_t* value) {
  if (_wcsicmp(value, L"foreground") == 0) {
    return Pick(lang, L"Foreground app", L"Активне вікно", L"Активное окно");
  }
  if (_wcsicmp(value, L"running") == 0) {
    return Pick(lang, L"Any running process", L"Будь-який запущений процес",
                L"Любой запущенный процесс");
  }
  return value;
}

inline const wchar_t* SpectrumStyleLabel(Lang lang, const wchar_t* value) {
  if (_wcsicmp(value, L"bars") == 0) {
    return Pick(lang, L"Bars", L"Стовпчики", L"Столбики");
  }
  if (_wcsicmp(value, L"orb") == 0) {
    return Pick(lang, L"Orb", L"Орб", L"Орб");
  }
  if (_wcsicmp(value, L"plasma") == 0) {
    return Pick(lang, L"Plasma", L"Плазма", L"Плазма");
  }
  if (_wcsicmp(value, L"led") == 0) {
    return Pick(lang, L"LED", L"LED", L"LED");
  }
  return value;
}

inline const wchar_t* ClickThroughModifierLabel(Lang lang, const wchar_t* value) {
  if (_wcsicmp(value, L"ctrl") == 0) {
    return Pick(lang, L"Ctrl", L"Ctrl", L"Ctrl");
  }
  if (_wcsicmp(value, L"alt") == 0) {
    return Pick(lang, L"Alt", L"Alt", L"Alt");
  }
  if (_wcsicmp(value, L"ctrl_or_alt") == 0) {
    return Pick(lang, L"Ctrl or Alt", L"Ctrl або Alt", L"Ctrl или Alt");
  }
  return value;
}

inline const wchar_t* EnumLabel(Lang lang, const SettingDef& def, size_t index) {
  if (!def.enums || index >= def.enumCount) {
    return L"";
  }
  if (wcscmp(def.key, L"Themes.BackdropMaterial") == 0 ||
      wcscmp(def.key, L"Appearance.BackdropMaterial") == 0) {
    return BackdropLabel(lang, def.enums[index].value);
  }
  if (wcscmp(def.key, L"Themes.AuraStyle") == 0) {
    return AuraLabel(lang, def.enums[index].value);
  }
  if (wcscmp(def.key, L"Appearance.TargetMonitor") == 0) {
    return TargetMonitorLabel(lang, def.enums[index].value);
  }
  if (wcscmp(def.key, L"Appearance.SpectrumStyle") == 0) {
    return SpectrumStyleLabel(lang, def.enums[index].value);
  }
  if (wcscmp(def.key, L"Appearance.ShapeStyle") == 0) {
    const wchar_t* v = def.enums[index].value;
    if (_wcsicmp(v, L"pill") == 0) {
      return Pick(lang, L"iPhone pill", L"Пігулка iPhone", L"Таблетка iPhone");
    }
    if (_wcsicmp(v, L"w11") == 0) {
      return Pick(lang, L"Windows 11 flyout", L"Виліт Windows 11", L"Вылет Windows 11");
    }
    if (_wcsicmp(v, L"notch") == 0) {
      return Pick(lang, L"macOS notch", L"Виріз macOS", L"Вырез macOS");
    }
  }
  if (wcscmp(def.key, L"Themes.ThemePreset") == 0) {
    return def.enums[index].label;
  }
  if (wcscmp(def.key, L"Appearance.Position") == 0) {
    const wchar_t* v = def.enums[index].value;
    if (_wcsicmp(v, L"top-center") == 0) {
      return Pick(lang, L"Top center", L"Зверху по центру", L"Сверху по центру");
    }
    if (_wcsicmp(v, L"top-left") == 0) {
      return Pick(lang, L"Top left", L"Зверху зліва", L"Сверху слева");
    }
    if (_wcsicmp(v, L"top-right") == 0) {
      return Pick(lang, L"Top right", L"Зверху справа", L"Сверху справа");
    }
    if (_wcsicmp(v, L"bottom-center") == 0) {
      return Pick(lang, L"Bottom center", L"Знизу по центру", L"Снизу по центру");
    }
    if (_wcsicmp(v, L"bottom-left") == 0) {
      return Pick(lang, L"Bottom left", L"Знизу зліва", L"Снизу слева");
    }
    if (_wcsicmp(v, L"bottom-right") == 0) {
      return Pick(lang, L"Bottom right", L"Знизу справа", L"Снизу справа");
    }
  }
  if (wcscmp(def.key, L"Behavior.ExpandMode") == 0) {
    const wchar_t* v = def.enums[index].value;
    if (_wcsicmp(v, L"hover") == 0) {
      return Pick(lang, L"Hover", L"Наведенням", L"Наведением");
    }
    if (_wcsicmp(v, L"click") == 0) {
      return Pick(lang, L"Click", L"Кліком", L"Кликом");
    }
    if (_wcsicmp(v, L"both") == 0) {
      return Pick(lang, L"Hover and click", L"Наведенням і кліком", L"Наведением и кликом");
    }
  }
  if (wcscmp(def.key, L"Behavior.ClickThroughModifier") == 0) {
    return ClickThroughModifierLabel(lang, def.enums[index].value);
  }
  if (wcscmp(def.key, L"Behavior.BlacklistMode") == 0) {
    return BlacklistModeLabel(lang, def.enums[index].value);
  }
  if (wcscmp(def.key, L"Appearance.ProgressStyle") == 0 ||
      wcscmp(def.key, L"Animations.AnimationStyle") == 0 ||
      wcscmp(def.key, L"Animations.AnimationSpeed") == 0 ||
      wcscmp(def.key, L"Themes.AccentColorMode") == 0 ||
      wcscmp(def.key, L"Themes.ContourBorderMode") == 0 ||
      wcscmp(def.key, L"Themes.CalendarAccent") == 0 ||
      wcscmp(def.key, L"Modules.FirstDayOfWeek") == 0) {
    const wchar_t* v = def.enums[index].value;
    if (_wcsicmp(v, L"slim") == 0) {
      return Pick(lang, L"Slim", L"Тонкий", L"Тонкий");
    }
    if (_wcsicmp(v, L"bar") == 0) {
      return Pick(lang, L"Bar", L"Смуга", L"Полоса");
    }
    if (_wcsicmp(v, L"wavy") == 0) {
      return Pick(lang, L"Wavy", L"Хвилястий", L"Волнистый");
    }
    if (_wcsicmp(v, L"squiggle") == 0) {
      return Pick(lang, L"Squiggle", L"Зигзаг", L"Зигзаг");
    }
    if (_wcsicmp(v, L"smooth") == 0) {
      return Pick(lang, L"Smooth", L"Плавний", L"Плавный");
    }
    if (_wcsicmp(v, L"default") == 0) {
      return Pick(lang, L"Default", L"Стандартний", L"Стандартный");
    }
    if (_wcsicmp(v, L"bouncy") == 0) {
      return Pick(lang, L"Bouncy", L"Пружний", L"Пружинный");
    }
    if (_wcsicmp(v, L"snappy") == 0) {
      return Pick(lang, L"Snappy", L"Швидкий", L"Резкий");
    }
    if (_wcsicmp(v, L"very-slow") == 0) {
      return Pick(lang, L"Very slow", L"Дуже повільно", L"Очень медленно");
    }
    if (_wcsicmp(v, L"slow") == 0) {
      return Pick(lang, L"Slow", L"Повільно", L"Медленно");
    }
    if (_wcsicmp(v, L"normal") == 0) {
      return Pick(lang, L"Normal", L"Нормально", L"Нормально");
    }
    if (_wcsicmp(v, L"fast") == 0) {
      return Pick(lang, L"Fast", L"Швидко", L"Быстро");
    }
    if (_wcsicmp(v, L"very-fast") == 0) {
      return Pick(lang, L"Very fast", L"Дуже швидко", L"Очень быстро");
    }
    if (_wcsicmp(v, L"ultra-fast") == 0) {
      return Pick(lang, L"Ultra fast", L"Ультрашвидко", L"Ультрабыстро");
    }
    if (_wcsicmp(v, L"auto") == 0) {
      return Pick(lang, L"Auto", L"Авто", L"Авто");
    }
    if (_wcsicmp(v, L"system") == 0) {
      return Pick(lang, L"System", L"Системний", L"Системный");
    }
    if (_wcsicmp(v, L"custom") == 0) {
      return Pick(lang, L"Custom", L"Власний", L"Свой");
    }
    if (_wcsicmp(v, L"borderless") == 0) {
      return Pick(lang, L"Borderless", L"Без рамки", L"Без рамки");
    }
    if (_wcsicmp(v, L"accent") == 0) {
      return Pick(lang, L"Accent", L"Акцент", L"Акцент");
    }
    if (_wcsicmp(v, L"monday") == 0) {
      return Pick(lang, L"Monday", L"Понеділок", L"Понедельник");
    }
    if (_wcsicmp(v, L"sunday") == 0) {
      return Pick(lang, L"Sunday", L"Неділя", L"Воскресенье");
    }
  }
  // Language / modifiers: keep schema labels (endonyms / chord names).
  return def.enums[index].label;
}

inline const wchar_t* CardTitle(Lang lang, SettingsCard card) {
  switch (card) {
  case SettingsCard::ThemeLook:
    return Pick(lang, L"Theme", L"Тема", L"Тема");
  case SettingsCard::ThemeAccent:
    return Pick(lang, L"Accent", L"Акцент", L"Акцент");
  case SettingsCard::ThemeSurface:
    return Pick(lang, L"Surface", L"Поверхня", L"Поверхность");
  case SettingsCard::ThemeBackdrop:
    return Pick(lang, L"Backdrop", L"Підложка", L"Подложка");
  case SettingsCard::ThemeAura:
    return Pick(lang, L"Aura", L"Ореол", L"Ореол");
  case SettingsCard::Media:
    return Pick(lang, L"Media", L"Медіа", L"Медиа");
  case SettingsCard::MediaClock:
    return Pick(lang, L"Clock and date", L"Годинник і дата", L"Часы и дата");
  case SettingsCard::Volume:
    return Pick(lang, L"Volume", L"Гучність", L"Громкость");
  case SettingsCard::Clipboard:
    return Pick(lang, L"Clipboard", L"Буфер обміну", L"Буфер обмена");
  case SettingsCard::Battery:
    return Pick(lang, L"Battery", L"Батарея", L"Батарея");
  case SettingsCard::Progress:
    return Pick(lang, L"Progress", L"Прогрес", L"Прогресс");
  case SettingsCard::CapsLock:
    return Pick(lang, L"Caps Lock", L"Caps Lock", L"Caps Lock");
  case SettingsCard::Weather:
    return Pick(lang, L"Weather", L"Погода", L"Погода");
  case SettingsCard::Calendar:
    return Pick(lang, L"Calendar", L"Календар", L"Календарь");
  case SettingsCard::Monitor:
    return Pick(lang, L"System monitor", L"Системний монітор", L"Системный монитор");
  case SettingsCard::Bluetooth:
    return Pick(lang, L"Bluetooth", L"Bluetooth", L"Bluetooth");
  case SettingsCard::DoNotDisturb:
    return Pick(lang, L"Do not disturb", L"Не турбувати", L"Не беспокоить");
  case SettingsCard::FocusTimer:
    return Pick(lang, L"Focus timer", L"Таймер фокусу", L"Таймер фокуса");
  case SettingsCard::Lyrics:
    return Pick(lang, L"Lyrics", L"Текст пісні", L"Текст песни");
  case SettingsCard::QuickLookup:
    return Pick(lang, L"Quick lookup", L"Швидкий пошук", L"Быстрый поиск");
  case SettingsCard::FileTray:
    return Pick(lang, L"File tray", L"Лоток файлів", L"Лоток файлов");
  case SettingsCard::GameOverlay:
    return Pick(lang, L"Game overlay", L"Ігровий оверлей", L"Игровой оверлей");
  case SettingsCard::Appearance:
    return GroupTitle(lang, SettingGroup::Appearance);
  case SettingsCard::Behavior:
    return GroupTitle(lang, SettingGroup::Behavior);
  case SettingsCard::Animations:
    return GroupTitle(lang, SettingGroup::Animations);
  case SettingsCard::Indicators:
    return GroupTitle(lang, SettingGroup::Indicators);
  case SettingsCard::Shortcuts:
    return GroupTitle(lang, SettingGroup::Shortcuts);
  case SettingsCard::General:
    return GroupTitle(lang, SettingGroup::General);
  default: {
    const SettingsCard unhandled = card;
    (void)unhandled;
    return Pick(lang, L"Settings", L"Налаштування", L"Настройки");
  }
  }
}

inline const wchar_t* PreviewTitle(Lang lang) {
  return Pick(lang, L"Preview", L"Попередній перегляд", L"Предпросмотр");
}

inline const wchar_t* ColorPickerLabel(Lang lang) {
  return Pick(lang, L"Choose color", L"Обрати колір", L"Выбрать цвет");
}

inline const wchar_t* NoMatches(Lang lang) {
  return Pick(lang, L"No matching settings", L"Немає збігів", L"Нет совпадений");
}

}  // namespace SettingsUiLoc
