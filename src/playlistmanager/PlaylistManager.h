/****************************************************************************************
 * Copyright (c) 2007 Bart Cerneels <bart.cerneels@kde.org>                             *
 *                                                                                      *
 * This program is free software; you can redistribute it and/or modify it under        *
 * the terms of the GNU General Public License as published by the Free Software        *
 * Foundation; either version 2 of the License, or (at your option) any later           *
 * version.                                                                             *
 *                                                                                      *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY      *
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A      *
 * PARTICULAR PURPOSE. See the GNU General Public License for more details.             *
 *                                                                                      *
 * You should have received a copy of the GNU General Public License along with         *
 * this program.  If not, see <http://www.gnu.org/licenses/>.                           *
 ****************************************************************************************/

#ifndef AMAROK_PLAYLISTMANAGER_H
#define AMAROK_PLAYLISTMANAGER_H

#include "core/support/Amarok.h"
#include "amarok_export.h"

#include "core/playlists/Playlist.h"
#include "core/playlists/PlaylistProvider.h"

#include "SyncedPlaylist.h"
#include "SyncRelationStorage.h"

#include <QMultiMap>
#include <QList>

class KJob;
class PlaylistManager;

namespace Playlists {
    class PlaylistFile;
    class PlaylistFileProvider;
    class UserPlaylistProvider;
    typedef KSharedPtr<PlaylistFile> PlaylistFilePtr;
}

namespace Podcasts {
    class PodcastProvider;
}

namespace The {
    AMAROK_EXPORT PlaylistManager* playlistManager();
}

typedef QList<Playlists::PlaylistProvider *> PlaylistProviderList;

/**
 * Facility for managing PlaylistProviders registered by other
 * parts of the application, plugins and scripts.
 */
class AMAROK_EXPORT PlaylistManager : public QObject
{
    Q_OBJECT

    public:
        enum PlaylistCategory
        {
            UserPlaylist = 1,
            PodcastChannel
        };

        static PlaylistManager *instance();
        static void destroy();

        /**
         * @returns all available categories registered at that moment
         */
        QList<int> availableCategories() { return m_providerMap.uniqueKeys(); }

        /**
         * returns playlists of a certain category from all registered PlaylistProviders
         */
        Playlists::PlaylistList playlistsOfCategory( int playlistCategory );

        /**
        * returns all PlaylistProviders that provider a certain playlist category.
        **/
        PlaylistProviderList providersForCategory( int playlistCategory );

        /**
         * Add a PlaylistProvider that contains Playlists of a category defined
         * in the PlaylistCategory enum.
         * @arg provider a PlaylistProvider
         * @arg category a default Category from the PlaylistManager::PlaylistCategory enum or a custom one registered before with registerCustomCategory.
         */
        void addProvider( Playlists::PlaylistProvider * provider, int category );

        /**
         * Remove a PlaylistProvider.
         * @arg provider a PlaylistProvider
         */

        void removeProvider( Playlists::PlaylistProvider * provider );

        Playlists::PlaylistProvider * playlistProvider( int category, QString name );

        void downloadPlaylist( const KUrl & path, const Playlists::PlaylistFilePtr playlist );

        /**
        *   Saves a list of tracks to a new SQL playlist. Used in the Playlist save button.
        *   @arg tracks list of tracks to save
        *   @arg name name of playlist to save
        */
        bool save( Meta::TrackList tracks, const QString &name = QString(),
                   Playlists::UserPlaylistProvider *toProvider = 0 );

        /**
         *  Saves a playlist from a file to the database.
         *  @arg fromLocation Saved playlist file to load
         */
         bool import( const QString &fromLocation );

        void rename( Playlists::PlaylistPtr playlist );

        bool deletePlaylists( Playlists::PlaylistList playlistlist );

        Podcasts::PodcastProvider *defaultPodcasts() { return m_defaultPodcastProvider; }
        Playlists::UserPlaylistProvider *defaultUserPlaylists() { return m_defaultUserPlaylistProvider; }

        /**
         *  Retrieves the provider owning the given playlist
         *  @arg playlist the playlist whose provider we want
         */
        QList<Playlists::PlaylistProvider*>
                getProvidersForPlaylist( const Playlists::PlaylistPtr playlist );

        /**
         *  Checks if the provider to whom this playlist belongs supports writing
         *  @arg playlist the playlist we are testing for writability
         *  @return whether or not the playlist is writable
         */

        bool isWritable( const Playlists::PlaylistPtr &playlist );

        void completePodcastDownloads();

    signals:
        void updated( int category );
        void categoryAdded( int category );
        void providerAdded( Playlists::PlaylistProvider *provider, int category );
        void providerRemoved( Playlists::PlaylistProvider *provider, int category );
        void playlistAdded( Playlists::PlaylistPtr playlist, int category );
        void playlistRemoved( Playlists::PlaylistPtr playlist, int category );

        void renamePlaylist( Playlists::PlaylistPtr playlist );

    private slots:
        void slotUpdated();
        void slotPlaylistAdded( Playlists::PlaylistPtr playlist );
        void slotPlaylistRemoved( Playlists::PlaylistPtr playlist );
        void downloadComplete( KJob *job );

    private:
        void loadPlaylists( Playlists::PlaylistProvider *provider, int category );
        void removePlaylists( Playlists::PlaylistProvider *provider );

        void addPlaylist( Playlists::PlaylistPtr playlist, int category );
        void removePlaylist( Playlists::PlaylistPtr playlist, int category );

        static PlaylistManager *s_instance;
        PlaylistManager();
        ~PlaylistManager();

        SyncRelationStorage *m_syncRelStore;

        bool shouldBeSynced( Playlists::PlaylistPtr playlist );

        Podcasts::PodcastProvider *m_defaultPodcastProvider;
        Playlists::UserPlaylistProvider *m_defaultUserPlaylistProvider;
        Playlists::PlaylistFileProvider *m_playlistFileProvider;

        QMultiMap<int, Playlists::PlaylistProvider*> m_providerMap; //Map PlaylistCategories to providers
        QMultiMap<int, Playlists::PlaylistPtr> m_playlistMap;
        QMultiMap<SyncedPlaylistPtr, Playlists::PlaylistPtr> m_syncedPlaylistMap;

        QMap<int, QString> m_customCategories;

        QMap<KJob *, Playlists::PlaylistFilePtr> m_downloadJobMap;
};

#endif
