#ifndef TEXTS_H
#define TEXTS_H

#include <QString>

// Тексты пульта из раздела "texts" config.json. Ключ — исходный текст из кода,
// значение — что показывать. Читаются один раз при запуске.
namespace Texts {

// Читает "texts" из config.json рядом с программой, затем из configPath поверх,
// и подключает подмену надписей из MainWindow.ui. Вызывать до создания окон.
void load(const QString &configPath);

QString get(const char *source);

} // namespace Texts

inline QString tx(const char *source)
{
    return Texts::get(source);
}

#endif // TEXTS_H
