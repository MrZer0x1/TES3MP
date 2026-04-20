#include "launchersettings.hpp"

#include <QTextStream>
#include <QString>
#include <QRegExp>
#include <QMultiMap>

#include <QDebug>

const char Config::LauncherSettings::sCurrentContentListKey[] = "Profiles/currentprofile";
const char Config::LauncherSettings::sLauncherConfigFileName[] = "launcher.cfg";
const char Config::LauncherSettings::sContentListsSectionPrefix[] = "Profiles/";
const char Config::LauncherSettings::sContentListSuffix[] = "/content";
const char Config::LauncherSettings::sGroundcoverSuffix[] = "/groundcover";
const char Config::LauncherSettings::sGroundcoverEnabledSuffix[] = "/groundcover-enabled";

namespace
{
    QString makeGroundcoverKey(const QString& contentListName)
    {
        return QString("Profiles/") + contentListName + QString("/groundcover");
    }

    QString makeGroundcoverEnabledKey(const QString& contentListName)
    {
        return QString("Profiles/") + contentListName + QString("/groundcover-enabled");
    }
}

QStringList Config::LauncherSettings::subKeys(const QString &key)
{
    QMultiMap<QString, QString> settings = SettingsBase::getSettings();
    QStringList keys = settings.uniqueKeys();

    QRegExp keyRe("(.+)/");

    QStringList result;

    for (const QString &currentKey : keys)
    {

        if (keyRe.indexIn(currentKey) != -1)
        {
            QString prefixedKey = keyRe.cap(1);

            if(prefixedKey.startsWith(key))
            {
                QString subKey = prefixedKey.remove(key);
                if (!subKey.isEmpty())
                    result.append(subKey);
            }
        }
    }

    result.removeDuplicates();
    return result;
}


bool Config::LauncherSettings::writeFile(QTextStream &stream)
{
    QString sectionPrefix;
    QRegExp sectionRe("([^/]+)/(.+)$");
    QMultiMap<QString, QString> settings = SettingsBase::getSettings();

    QMapIterator<QString, QString> i(settings);
    i.toBack();

    while (i.hasPrevious()) {
        i.previous();

        QString prefix;
        QString key;

        if (sectionRe.exactMatch(i.key())) {
             prefix = sectionRe.cap(1);
             key = sectionRe.cap(2);
        }

        // Get rid of legacy settings
        if (key.contains(QChar('\\')))
            continue;

        if (key == QLatin1String("CurrentProfile"))
            continue;

        if (sectionPrefix != prefix) {
            sectionPrefix = prefix;
            stream << "\n[" << prefix << "]\n";
        }

        stream << key << "=" << i.value() << "\n";
    }

    return true;

}

QStringList Config::LauncherSettings::getContentLists()
{
    return subKeys(QString(sContentListsSectionPrefix));
}

QString Config::LauncherSettings::makeContentListKey(const QString& contentListName)
{
    return QString(sContentListsSectionPrefix) + contentListName + QString(sContentListSuffix);
}

void Config::LauncherSettings::setContentList(const GameSettings& gameSettings)
{
    const QStringList contentFiles(gameSettings.getContentList());
    const QStringList groundcoverFiles(gameSettings.getGroundcoverList());

    QStringList files(contentFiles);
    files.append(groundcoverFiles);

    if (files.isEmpty())
        return;

    const bool groundcoverEnabled = !groundcoverFiles.isEmpty();

    for (const QString &listName : getContentLists())
    {
        if (isEqual(files, getContentListFiles(listName))
            && isEqual(groundcoverFiles, getGroundcoverFiles(listName))
            && groundcoverEnabled == isGroundcoverEnabled(listName))
        {
            setCurrentContentListName(listName);
            return;
        }
    }

    QString newContentListName(makeNewContentListName());
    setCurrentContentListName(newContentListName);
    setContentList(newContentListName, files, groundcoverFiles, groundcoverEnabled);
}

void Config::LauncherSettings::removeContentList(const QString &contentListName)
{
    remove(makeContentListKey(contentListName));
    remove(makeGroundcoverKey(contentListName));
    remove(makeGroundcoverEnabledKey(contentListName));
}

void Config::LauncherSettings::setCurrentContentListName(const QString &contentListName)
{
    remove(QString(sCurrentContentListKey));
    setValue(QString(sCurrentContentListKey), contentListName);
}

void Config::LauncherSettings::setContentList(const QString& contentListName, const QStringList& fileNames,
                                              const QStringList& groundcoverFileNames, bool groundcoverEnabled)
{
    removeContentList(contentListName);

    QString key = makeContentListKey(contentListName);
    for (const QString& fileName : fileNames)
    {
        if (!groundcoverFileNames.contains(fileName, Qt::CaseInsensitive))
            setMultiValue(key, fileName);
    }

    QString groundcoverKey = makeGroundcoverKey(contentListName);
    for (const QString& fileName : groundcoverFileNames)
        setMultiValue(groundcoverKey, fileName);

    setValue(makeGroundcoverEnabledKey(contentListName), groundcoverEnabled ? QLatin1String("true") : QLatin1String("false"));
}

QString Config::LauncherSettings::getCurrentContentListName() const
{
    return value(QString(sCurrentContentListKey));
}

QStringList Config::LauncherSettings::getContentListFiles(const QString& contentListName) const
{
    QStringList result = reverse(getSettings().values(makeContentListKey(contentListName)));
    result.append(getGroundcoverFiles(contentListName));
    return result;
}

QStringList Config::LauncherSettings::getGroundcoverFiles(const QString& contentListName) const
{
    return reverse(getSettings().values(makeGroundcoverKey(contentListName)));
}

bool Config::LauncherSettings::isGroundcoverEnabled(const QString& contentListName) const
{
    return value(makeGroundcoverEnabledKey(contentListName)) == QLatin1String("true");
}

QStringList Config::LauncherSettings::reverse(const QStringList& toReverse)
{
    QStringList result;
    result.reserve(toReverse.size());
    std::reverse_copy(toReverse.begin(), toReverse.end(), std::back_inserter(result));
    return result;
}

bool Config::LauncherSettings::isEqual(const QStringList& list1, const QStringList& list2)
{
    if (list1.count() != list2.count())
        return false;

    for (int i = 0; i < list1.count(); ++i)
    {
        if (list1.at(i) != list2.at(i))
            return false;
    }

    return true;
}

QString Config::LauncherSettings::makeNewContentListName()
{
    time_t rawtime;
    struct tm * timeinfo;

    time(&rawtime);
    timeinfo = localtime(&rawtime);
    int base = 10;
    QChar zeroPad('0');
    return QString("%1-%2-%3T%4:%5:%6")
        .arg(timeinfo->tm_year + 1900, 4).arg(timeinfo->tm_mon + 1, 2, base, zeroPad).arg(timeinfo->tm_mday, 2, base, zeroPad)
        .arg(timeinfo->tm_hour, 2).arg(timeinfo->tm_min, 2, base, zeroPad).arg(timeinfo->tm_sec, 2, base, zeroPad);
}
