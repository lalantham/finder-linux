#pragma once

#include <QSet>
#include <QString>

// Shared mapping from a search/filter "kind" name to the file extensions that
// belong to it. Both the file model's instant filter and the recursive search
// engine classify files with the same tables; keeping them in one place stops
// the two from drifting apart (an extension added to one but not the other
// silently changes what a search returns).
namespace FileKind {

enum class Category {
    None, // not a recognised kind name; caller decides how to match
    Folder,
    Image,
    Document,
    Video,
    Audio,
    Archive
};

// Maps a user-supplied kind name ("image", "images", "kind:image", ...) to a
// category. Case-insensitive; returns Category::None for unknown names so the
// caller can fall back to its own MIME- or display-name-based matching.
[[nodiscard]] Category fromFilterName(const QString& name);

// Extensions belonging to `category`, lower-case and without a leading dot.
// Empty for Category::None and Category::Folder.
[[nodiscard]] const QSet<QString>& extensionsFor(Category category);

// Convenience: true when `suffix` (with or without a leading dot, any case) is
// one of the extensions for `category`.
[[nodiscard]] bool matchesExtension(const QString& suffix, Category category);

} // namespace FileKind
