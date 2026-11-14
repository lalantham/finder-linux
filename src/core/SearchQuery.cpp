#include "SearchQuery.h"

#include <QRegularExpression>
#include <QStringList>

namespace {

qint64 unitMultiplier(QString unit) {
    unit = unit.trimmed().toUpper();
    if (unit == QStringLiteral("TB"))
        return 1000LL * 1000LL * 1000LL * 1000LL;
    if (unit == QStringLiteral("GB"))
        return 1000LL * 1000LL * 1000LL;
    if (unit == QStringLiteral("MB"))
        return 1000LL * 1000LL;
    if (unit == QStringLiteral("KB"))
        return 1000LL;
    return 1LL;
}

bool parseSize(const QString& text, qint64* value) {
    QString clean = text.trimmed();
    clean.remove(QLatin1Char(' '));
    if (clean.isEmpty())
        return false;

    QString number = clean;
    QString unit;
    static const QRegularExpression re(QStringLiteral(R"(^([0-9]+(?:\.[0-9]+)?)([KMGTP]?B)?$)"),
                                       QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = re.match(clean);
    if (match.hasMatch()) {
        number = match.captured(1);
        unit = match.captured(2);
    } else if (clean.endsWith(QLatin1Char('B'), Qt::CaseInsensitive)) {
        number.chop(1);
        unit = QStringLiteral("B");
    }

    bool ok = false;
    const double parsed = number.toDouble(&ok);
    if (!ok || parsed < 0.0)
        return false;
    *value = static_cast<qint64>(parsed * static_cast<double>(unitMultiplier(unit)));
    return true;
}

bool parseDate(const QString& text, QDate* date) {
    QString clean = text.trimmed();
    if (clean.isEmpty())
        return false;
    QDate parsed = QDate::fromString(clean, Qt::ISODate);
    if (!parsed.isValid()) {
        parsed = QDate::fromString(clean, QStringLiteral("yyyy/MM/dd"));
    }
    if (!parsed.isValid())
        return false;
    *date = parsed;
    return true;
}

void applySizeExpression(const QString& expression, SearchFilter* filter) {
    const QString value = expression.trimmed();
    const qsizetype range = value.indexOf(QStringLiteral(".."));
    if (range >= 0) {
        qint64 minimum = -1;
        qint64 maximum = -1;
        parseSize(value.left(range), &minimum);
        parseSize(value.mid(range + 2), &maximum);
        if (minimum >= 0)
            filter->minSize = minimum;
        if (maximum >= 0)
            filter->maxSize = maximum;
        return;
    }

    qint64 size = -1;
    if (value.startsWith(QStringLiteral(">="))) {
        if (parseSize(value.mid(2), &size))
            filter->minSize = size;
    } else if (value.startsWith(QStringLiteral("<="))) {
        if (parseSize(value.mid(2), &size))
            filter->maxSize = size;
    } else if (value.startsWith(QLatin1Char('>'))) {
        if (parseSize(value.mid(1), &size))
            filter->minSize = size + 1;
    } else if (value.startsWith(QLatin1Char('<'))) {
        if (parseSize(value.mid(1), &size))
            filter->maxSize = size > 0 ? size - 1 : 0;
    } else if (parseSize(value, &size)) {
        filter->minSize = size;
        filter->maxSize = size;
    }
}

void applyDateExpression(const QString& expression, SearchFilter* filter) {
    const QString value = expression.trimmed();
    const qsizetype range = value.indexOf(QStringLiteral(".."));
    if (range >= 0) {
        QDate from;
        QDate to;
        if (parseDate(value.left(range), &from))
            filter->modifiedFrom = from;
        if (parseDate(value.mid(range + 2), &to))
            filter->modifiedTo = to;
        return;
    }

    QDate date;
    if (value.startsWith(QStringLiteral(">="))) {
        if (parseDate(value.mid(2), &date))
            filter->modifiedFrom = date;
    } else if (value.startsWith(QStringLiteral("<="))) {
        if (parseDate(value.mid(2), &date))
            filter->modifiedTo = date;
    } else if (value.startsWith(QLatin1Char('>'))) {
        if (parseDate(value.mid(1), &date))
            filter->modifiedFrom = date.addDays(1);
    } else if (value.startsWith(QLatin1Char('<'))) {
        if (parseDate(value.mid(1), &date))
            filter->modifiedTo = date.addDays(-1);
    } else if (parseDate(value, &date)) {
        filter->modifiedFrom = date;
        filter->modifiedTo = date;
    }
}

QString formatBytes(qint64 bytes) {
    if (bytes < 1000)
        return QStringLiteral("%1 B").arg(bytes);
    static const char* units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1000.0 && unit < 5) {
        value /= 1000.0;
        ++unit;
    }
    return QStringLiteral("%1 %2")
        .arg(value, 0, 'f', value < 10.0 ? 1 : 0)
        .arg(QLatin1String(units[unit]));
}

QString stripQuotes(const QString& value) {
    QString result = value.trimmed();
    if (result.size() >= 2 && result.startsWith(QLatin1Char('"')) &&
        result.endsWith(QLatin1Char('"'))) {
        result = result.mid(1, result.size() - 2);
    }
    return result;
}

} // namespace

namespace SearchQuery {

QStringList tokenize(const QString& query) {
    QStringList tokens;
    QString current;
    bool quoted = false;
    bool escaped = false;

    for (const QChar ch : query) {
        if (escaped) {
            current.append(ch);
            escaped = false;
            continue;
        }
        if (ch == QLatin1Char('\\')) {
            escaped = true;
            continue;
        }
        if (ch == QLatin1Char('"')) {
            quoted = !quoted;
            continue;
        }
        if (ch.isSpace() && !quoted) {
            if (!current.isEmpty()) {
                tokens.append(current);
                current.clear();
            }
            continue;
        }
        current.append(ch);
    }
    if (!current.isEmpty()) {
        tokens.append(current);
    }
    return tokens;
}

QString kindFromToken(const QString& token) {
    const QString lower = token.trimmed().toLower();
    if (lower == QStringLiteral("image") || lower == QStringLiteral("images") ||
        lower == QStringLiteral("picture") || lower == QStringLiteral("pictures")) {
        return QStringLiteral("images");
    }
    if (lower == QStringLiteral("document") || lower == QStringLiteral("documents") ||
        lower == QStringLiteral("doc") || lower == QStringLiteral("docs") ||
        lower == QStringLiteral("text")) {
        return QStringLiteral("documents");
    }
    if (lower == QStringLiteral("video") || lower == QStringLiteral("videos") ||
        lower == QStringLiteral("movie") || lower == QStringLiteral("movies")) {
        return QStringLiteral("videos");
    }
    if (lower == QStringLiteral("audio") || lower == QStringLiteral("music") ||
        lower == QStringLiteral("sound")) {
        return QStringLiteral("audio");
    }
    if (lower == QStringLiteral("folder") || lower == QStringLiteral("folders") ||
        lower == QStringLiteral("directory") || lower == QStringLiteral("directories")) {
        return QStringLiteral("folders");
    }
    if (lower == QStringLiteral("archive") || lower == QStringLiteral("archives") ||
        lower == QStringLiteral("zip")) {
        return QStringLiteral("archives");
    }
    return {};
}

SearchFilter parse(const QString& query) {
    SearchFilter filter;
    filter.query = query.trimmed();

    const QStringList tokens = tokenize(filter.query);
    for (const QString& rawToken : tokens) {
        if (rawToken.isEmpty())
            continue;

        QString token = rawToken;
        bool excluded = false;
        if (token.startsWith(QLatin1Char('-')) && token.size() > 1) {
            excluded = true;
            token = token.mid(1);
        }

        const qsizetype colon = token.indexOf(QLatin1Char(':'));
        QString key;
        QString value;
        if (colon > 0) {
            key = token.left(colon).trimmed().toLower();
            value = stripQuotes(token.mid(colon + 1));
        } else {
            value = stripQuotes(token);
        }

        if (key == QStringLiteral("name")) {
            if (!value.isEmpty()) {
                if (excluded)
                    filter.excludeTerms.append(value);
                else
                    filter.nameTerms.append(value);
            }
            continue;
        }
        if (key == QStringLiteral("content") || key == QStringLiteral("text") ||
            key == QStringLiteral("inside")) {
            if (!value.isEmpty() && !excluded) {
                filter.contentTerms.append(value);
                filter.searchContents = true;
            }
            continue;
        }
        if (key == QStringLiteral("kind") || key == QStringLiteral("type")) {
            const QString kind = kindFromToken(value);
            if (!kind.isEmpty())
                filter.kindFilter = kind;
            continue;
        }
        if (key == QStringLiteral("tag") || key == QStringLiteral("tags")) {
            for (const QString& tag : value.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                const QString clean = tag.trimmed();
                if (!clean.isEmpty() && !filter.tags.contains(clean)) {
                    filter.tags.append(clean);
                }
            }
            continue;
        }
        if (key == QStringLiteral("ext") || key == QStringLiteral("extension")) {
            for (const QString& ext : value.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                QString clean = ext.trimmed().toLower();
                if (clean.startsWith(QLatin1Char('.')))
                    clean.remove(0, 1);
                if (!clean.isEmpty() && !filter.extensions.contains(clean)) {
                    filter.extensions.append(clean);
                }
            }
            continue;
        }
        if (key == QStringLiteral("size")) {
            applySizeExpression(value, &filter);
            continue;
        }
        if (key == QStringLiteral("before")) {
            applyDateExpression(QStringLiteral("<") + value, &filter);
            continue;
        }
        if (key == QStringLiteral("after")) {
            applyDateExpression(QStringLiteral(">") + value, &filter);
            continue;
        }
        if (key == QStringLiteral("modified") || key == QStringLiteral("date")) {
            applyDateExpression(value, &filter);
            continue;
        }

        // Bare word or an unknown key:value token. Preserve it as a name term.
        const QString term = (colon > 0 && !key.isEmpty()) ? token : value;
        if (!term.isEmpty()) {
            if (excluded)
                filter.excludeTerms.append(term);
            else
                filter.nameTerms.append(term);
        }
    }

    return filter;
}

QString summary(const SearchFilter& filter) {
    QStringList chips;
    if (!filter.nameTerms.isEmpty()) {
        chips.append(QStringLiteral("name: %1").arg(filter.nameTerms.join(QLatin1Char(' '))));
    }
    if (!filter.contentTerms.isEmpty()) {
        chips.append(QStringLiteral("content: %1").arg(filter.contentTerms.join(QLatin1Char(' '))));
    }
    if (!filter.kindFilter.isEmpty()) {
        chips.append(QStringLiteral("kind: %1").arg(filter.kindFilter));
    }
    if (!filter.tags.isEmpty()) {
        chips.append(QStringLiteral("tag: %1").arg(filter.tags.join(QLatin1Char(','))));
    }
    if (!filter.extensions.isEmpty()) {
        chips.append(QStringLiteral("ext: %1").arg(filter.extensions.join(QLatin1Char(','))));
    }
    if (filter.minSize >= 0) {
        chips.append(QStringLiteral("≥ %1").arg(formatBytes(filter.minSize)));
    }
    if (filter.maxSize >= 0) {
        chips.append(QStringLiteral("≤ %1").arg(formatBytes(filter.maxSize)));
    }
    if (filter.modifiedFrom.isValid()) {
        chips.append(QStringLiteral("from %1").arg(filter.modifiedFrom.toString(Qt::ISODate)));
    }
    if (filter.modifiedTo.isValid()) {
        chips.append(QStringLiteral("to %1").arg(filter.modifiedTo.toString(Qt::ISODate)));
    }
    return chips.join(QStringLiteral("   •   "));
}

} // namespace SearchQuery
