#pragma once

#include "core/RadioStation.h"

#include <QByteArray>
#include <QString>

// Pulls the real stream URL out of a fetched playlist file. Pure string work:
// the fetching itself lives in RadioPlayer.
namespace PlaylistResolver {

// First File1=/FileN= entry of a SHOUTcast/Icecast .pls file.
QString parsePls(const QByteArray& bytes);

// First non-comment, non-blank line of an .m3u file.
QString parseM3u(const QByteArray& bytes);

// Dispatches on the type. Direct returns an empty string, since there is
// nothing to resolve.
QString firstStreamUrl(StreamType type, const QByteArray& bytes);

}  // namespace PlaylistResolver
