#pragma once

#include "settings_schema.h"
#include "settings_store.h"

#include <windows.h>

#include <string>

// UI strings for the settings dialog (standalone). Independent of .wh.cpp Loc().
namespace SettingsUiLoc {

enum class Lang { En, Uk, Ru };

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
  return Pick(lang, L"Cancel", L"Скасувати", L"Отмена");
}

inline const wchar_t* Autostart(Lang lang) {
  return Pick(lang, L"Autostart with Windows", L"Автозапуск з Windows", L"Автозапуск с Windows");
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
  case SettingGroup::Runtime:
    return Pick(lang, L"Runtime", L"Службове", L"Служебное");
  }
  return Pick(lang, L"Other", L"Інше", L"Прочее");
}

inline const wchar_t* SettingLabel(Lang lang, const wchar_t* key) {
  if (wcscmp(key, L"Appearance.Position") == 0) {
    return Pick(lang, L"Position", L"Позиція", L"Позиция");
  }
  if (wcscmp(key, L"Behavior.AlwaysOnTop") == 0) {
    return Pick(lang, L"Always on top", L"Поверх усіх вікон", L"Поверх всех окон");
  }
  if (wcscmp(key, L"Behavior.ExpandOnHover") == 0) {
    return Pick(lang, L"Expand on hover", L"Розгортати наведенням", L"Раскрывать наведением");
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
  if (wcscmp(key, L"Modules.Volume") == 0) {
    return Pick(lang, L"Volume", L"Гучність", L"Громкость");
  }
  if (wcscmp(key, L"Modules.Brightness") == 0) {
    return Pick(lang, L"Brightness", L"Яскравість", L"Яркость");
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
  if (wcscmp(key, L"General.Language") == 0) {
    return Pick(lang, L"Language", L"Мова", L"Язык");
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
  return value;
}

inline const wchar_t* AuraLabel(Lang lang, const wchar_t* value) {
  if (_wcsicmp(value, L"off") == 0) {
    return Pick(lang, L"Off", L"Вимкнено", L"Выключено");
  }
  if (_wcsicmp(value, L"glow") == 0) {
    return Pick(lang, L"Glow", L"Свічення", L"Свечение");
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

inline const wchar_t* EnumLabel(Lang lang, const SettingDef& def, size_t index) {
  if (!def.enums || index >= def.enumCount) {
    return L"";
  }
  if (wcscmp(def.key, L"Themes.BackdropMaterial") == 0) {
    return BackdropLabel(lang, def.enums[index].value);
  }
  if (wcscmp(def.key, L"Themes.AuraStyle") == 0) {
    return AuraLabel(lang, def.enums[index].value);
  }
  // Language / modifiers: keep schema labels (endonyms / chord names).
  return def.enums[index].label;
}

}  // namespace SettingsUiLoc
