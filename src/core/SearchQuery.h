#pragma once

#include <QDate>
#include <QString>
#include <QStringList>
#include <QVector>

// Parsed representation of a structured search query.
//
// Supported tokens:
//   name:foo            match the file name
//   content:"foo bar"   search inside text files and PDFs
//   kind:image          image, document, video, audio, folder, archive
//   tag:Red,Work        require all listed tags
//   ext:pdf,md          require one of the extensions
//   size:>10MB          size comparison or range (1MB..10MB)
//   modified:>2024-01-01  modification date comparison or range
//   -foo                exclude files whose name/tags/comment match
//
// Bare words are matched against the name, tags, and comment.
struct SearchFilter {
    QString query;            // Original user-visible query text
    QStringList nameTerms;    // All must match name/tags/comment
    QStringList excludeTerms; // None may match name/tags/comment
    QStringList contentTerms; // All must occur in the file contents

    QString searchPath;
    QStringList searchPaths; // Used for "This PC" / multi-root searches
    bool thisPC{false};
    bool recursive{true};
    bool caseSensitive{false};
    bool includeHidden{false};
    bool searchContents{false};
    bool useIndex{false};

    QString kindFilter;
    QStringList tags;
    QStringList extensions;
    qint64 minSize{-1};
    qint64 maxSize{-1};
    QDate modifiedFrom;
    QDate modifiedTo;
};

struct SmartFolder {
    QString name;
    SearchFilter filter;

    [[nodiscard]] bool isValid() const {
        return !name.trimmed().isEmpty();
    }
};

namespace SearchQuery {

// Parse a query string into filters. Unknown key:value tokens are treated as
// ordinary name terms so strings such as "http://example" keep working.
[[nodiscard]] SearchFilter parse(const QString& query);

// Human-readable summary of the active structured filters, suitable for a
// status/chip bar.
[[nodiscard]] QString summary(const SearchFilter& filter);

[[nodiscard]] QString kindFromToken(const QString& token);
[[nodiscard]] QStringList tokenize(const QString& query);

// A structured filter token suitable for a removable UI chip.
struct Chip {
    QString label;
    QString token; // Canonical raw token, safe to pass to removeToken().
    bool excluded{false};
};

[[nodiscard]] QVector<Chip> chips(const QString& query);
// Remove the first token whose canonical form equals rawToken, preserving
// surrounding free-text terms.
[[nodiscard]] QString removeToken(const QString& query, const QString& rawToken);

} // namespace SearchQuery
