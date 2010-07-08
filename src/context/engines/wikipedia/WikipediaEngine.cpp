/****************************************************************************************
 * Copyright (c) 2007 Leo Franchi <lfranchi@gmail.com>                                  *
 * Copyright (c) 2008 Mark Kretschmann <kretschmann@kde.org>                            *
 * Copyright (c) 2009 Simon Esneault <simon.esneault@gmail.com>                         *
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

#define DEBUG_PREFIX "WikipediaEngine"

#include "WikipediaEngine.h"

#include "core/support/Amarok.h"
#include "core/support/Debug.h"
#include "EngineController.h"

#include <Plasma/DataContainer>

#include <QHashIterator>
#include <QXmlStreamReader>

using namespace Context;

class WikipediaEnginePrivate
{
private:
    WikipediaEngine *const q_ptr;
    Q_DECLARE_PUBLIC( WikipediaEngine )

public:
    WikipediaEnginePrivate( WikipediaEngine *parent )
        : q_ptr( parent )
        , currentSelection( Artist )
        , dataContainer( 0 )
    {}
    ~WikipediaEnginePrivate() {}

    enum SelectionType
    {
        Artist,
        Album,
        Track
    };

    // functions
    void checkRequireUpdate( Meta::TrackPtr track );
    void fetchWikiUrl( const QString &title, const QString &urlPrefix );
    void fetchLangLinks( const QString &title, const QString &llcontinue = QString() );
    void reloadWikipedia();
    bool setSelection( SelectionType type ); // returns true if selection is changed
    bool setSelection( const QString &type );
    SelectionType selection() const;
    void updateEngine();
    QString wikiParse();

    // data members
    SelectionType currentSelection;
    QString wiki;
    QUrl wikiCurrentUrl;
    QString wikiLanguagesSection;
    QStringList preferredLangs;
    QString currentWikiLang;
    Meta::TrackPtr currentTrack;

    Plasma::DataContainer *dataContainer;

    QSet< QUrl > urls;

    // private slots
    void _dataContainerUpdated( const QString &source, const Plasma::DataEngine::Data &data );
    void _parseLangLinksResult( const KUrl &url, QByteArray data, NetworkAccessManagerProxy::Error e );
    void _wikiResult( const KUrl &url, QByteArray result, NetworkAccessManagerProxy::Error e );
};

void
WikipediaEnginePrivate::_dataContainerUpdated( const QString &source, const Plasma::DataEngine::Data &data )
{
    Q_Q( WikipediaEngine );

    if( source != "wikipedia" )
        return;

    if( data.contains( "reload" ) )
    {
        debug() << "reloading";
        if( data.value( "reload" ).toBool() )
            reloadWikipedia();
        q->removeData( source, "reload" );
    }

    if( data.contains( "goto" ) )
    {
        QString gotoType = data.value( "goto" ).toString();
        debug() << "goto:" << gotoType;
        if( !gotoType.isEmpty() )
        {
            setSelection( gotoType );
            q->setData( source, "busy", "busy" );
            updateEngine();
        }
        q->removeData( source, "goto" );
    }

    if( data.contains( "clickUrl" ) )
    {
        QUrl clickUrl = data.value( "clickUrl" ).toUrl();
        debug() << "clickUrl:" << clickUrl;
        if( clickUrl.isValid() )
        {
            wikiCurrentUrl = clickUrl;
            if( !wikiCurrentUrl.hasQueryItem( "useskin" ) )
                wikiCurrentUrl.addQueryItem( "useskin", "monobook" );
            urls << wikiCurrentUrl;
            q->setData( source, "busy", "busy" );
            The::networkAccessManager()->getData( wikiCurrentUrl, q,
                 SLOT(_wikiResult(KUrl,QByteArray,NetworkAccessManagerProxy::Error)) );
        }
        q->removeData( source, "clickUrl" );
    }

    if( data.contains( "lang" ) )
    {
        QStringList langList = data.value( "lang" ).toStringList();
        if( !langList.isEmpty() && (preferredLangs != langList) )
        {
            preferredLangs = langList;
            debug() << "updated preferred wikipedia languages:" << preferredLangs;
        }
        q->removeData( source, "lang" );
    }
}

void
WikipediaEnginePrivate::_wikiResult( const KUrl &url, QByteArray result, NetworkAccessManagerProxy::Error e )
{
    Q_Q( WikipediaEngine );
    if( !urls.contains( url ) )
        return;

    urls.remove( url );
    if( e.code != QNetworkReply::NoError )
    {
        q->removeData( "wikipedia", "busy" );
        q->removeAllData( "wikipedia" );
        q->setData( "wikipedia", "message", i18n("Unable to retrieve Wikipedia information: %1", e.description) );
        q->scheduleSourcesUpdated();
        return;
    }

    debug() << "Received page from wikipedia:" << url;
    wiki = result;

    // FIXME: For now we test if we got an article or not with a test on this string "wgArticleId=0"
    // This is bad
    if( wiki.contains("wgArticleId=0") &&
        (wiki.contains("wgNamespaceNumber=0") ||
         wiki.contains("wgPageName=\"Special:Badtitle\"") ) ) // The article does not exist
    {
        debug() << "article does not exist";
        q->removeAllData( "wikipedia" );
        q->setData( "wikipedia", "message", i18n( "No information found..." ) );
        q->scheduleSourcesUpdated();
        return;
    }

    // We've found a page
    DataEngine::Data data;
    data["page"] = wikiParse();
    data["url"] = QUrl(url);

    if( currentSelection == Artist ) // default, or applet told us to fetch artist
    {
        if( currentTrack->artist() )
        {
            data["label"] =  "Artist";
            data["title"] = currentTrack->artist()->prettyName();
        }
    }
    else if( currentSelection == Track )
    {
        data["label"] = "Title";
        data["title"] = currentTrack->prettyName();
    }
    else if( currentSelection == Album )
    {
        if( currentTrack->album() )
        {
            data["label"] = "Album";
            data["title"] = currentTrack->album()->prettyName();
        }
    }
    q->removeData( "wikipedia", "busy" );
    q->setData( "wikipedia", data );
    q->scheduleSourcesUpdated();
}

void
WikipediaEnginePrivate::_parseLangLinksResult( const KUrl &url, QByteArray data,
                                               NetworkAccessManagerProxy::Error e )
{
    Q_UNUSED( url );
    Q_Q( WikipediaEngine );

    if( e.code != QNetworkReply::NoError || data.isEmpty() )
    {
        debug() << "Parsing langlinks result failed" << e.description;
        q->removeAllData( "wikipedia" );
        q->setData( "wikipedia", "message", i18n("Unable to retrieve Wikipedia information: %1", e.description) );
        q->scheduleSourcesUpdated();
        return;
    }

    QHash<QString, QString> langTitleMap;
    QString llcontinue;
    QXmlStreamReader xml( data );
    while( !xml.atEnd() && !xml.hasError() )
    {
        xml.readNext();
        if( xml.isStartElement() && xml.name() == "page" )
        {
            if( xml.attributes().hasAttribute("missing") )
                break;

            while( !xml.atEnd() )
            {
                xml.readNext();
                if( xml.isEndElement() && xml.name() == "page" )
                    break;

                if( xml.isStartElement() )
                {
                    if( xml.name() == "ll" )
                    {
                        QXmlStreamAttributes a = xml.attributes();
                        if( a.hasAttribute("lang") )
                        {
                            QString lang = a.value( "lang" ).toString();
                            langTitleMap[lang] = xml.readElementText();
                        }
                    }
                    else if( xml.name() == "query-continue" )
                    {
                        xml.readNext();
                        if( xml.isStartElement() && xml.name() == "langlinks" )
                        {
                            QXmlStreamAttributes a = xml.attributes();
                            if( a.hasAttribute("llcontinue") )
                                llcontinue = a.value( "llcontinue" ).toString();
                        }
                    }
                }
            }
        }
    }

    q->removeData( "wikipedia", "busy" );
    /* Since we query langlinks using the English Wikipedia host, interwiki
     * langlinks results will not contain English pages. So we need to manually
     * add it here. */
    if( preferredLangs.contains("en") )
        langTitleMap["en"] = url.queryItemValue( "titles" );

    QStringListIterator langIter( preferredLangs );
    while( langIter.hasNext() )
    {
        const QString &lang = langIter.next();
        const QStringList &prefixes = lang.split( QChar(':') );
        const QString &urlPrefix = (prefixes.count() == 1) ? prefixes.front() : prefixes.back();
        if( langTitleMap.contains(urlPrefix) )
        {
            const QString &title = langTitleMap.value( urlPrefix );
            fetchWikiUrl( title, urlPrefix );
            return;
        }
    }

    if( !llcontinue.isEmpty() )
    {
        const QString &title = url.queryItemValue( "titles" );
        fetchLangLinks( title, llcontinue );
        return;
    }
}

void
WikipediaEnginePrivate::checkRequireUpdate( Meta::TrackPtr track )
{
    if( !track )
        return;

    bool needUpdate( false );
    if( !currentTrack )
    {
        currentTrack = track;
        needUpdate = true;
    }
    else
    {
        switch( currentSelection )
        {
        case WikipediaEnginePrivate::Artist:
            needUpdate = track->artist()->name() != currentTrack->artist()->name();
            break;

        case WikipediaEnginePrivate::Album:
            needUpdate = track->album()->name() != currentTrack->album()->name();
            break;

        case WikipediaEnginePrivate::Track:
            needUpdate = track->name() != currentTrack->name();
            break;
        }
    }

    if( needUpdate )
    {
        currentTrack = track;
        updateEngine();
    }
}

void
WikipediaEnginePrivate::fetchWikiUrl( const QString &title, const QString &urlPrefix )
{
    Q_Q( WikipediaEngine );
    // We now use:  http://en.wikipedia.org/w/index.php?title=The_Beatles&useskin=monobook
    // instead of:  http://en.wikipedia.org/wiki/The_Beatles
    // So that wikipedia skin is forced to default "monoskin", and the page can be parsed correctly (see BUG 205901 )
    currentWikiLang = urlPrefix;
    KUrl pageUrl;
    pageUrl.setScheme( "http" );
    pageUrl.setHost( urlPrefix + ".wikipedia.org" );
    pageUrl.setPath( "/w/index.php" );
    pageUrl.addQueryItem( "useskin", "monobook" );
    pageUrl.addQueryItem( "title", title );
    wikiCurrentUrl = pageUrl;
    urls << pageUrl;
    q->setData( "wikipedia", "busy", "busy" );
    The::networkAccessManager()->getData( pageUrl, q,
         SLOT(_wikiResult(KUrl,QByteArray,NetworkAccessManagerProxy::Error)) );
}

void
WikipediaEnginePrivate::fetchLangLinks( const QString &title, const QString &llcontinue )
{
    Q_Q( WikipediaEngine );

    KUrl url;
    url.setScheme( "http" );
    url.setHost( "en.wikipedia.org" );
    url.setPath( "/w/api.php" );
    url.addQueryItem( "action", "query" );
    url.addQueryItem( "prop", "langlinks" );
    url.addQueryItem( "titles", title );
    url.addQueryItem( "format", "xml" );
    url.addQueryItem( "lllimit", QString::number(100) );
    if( !llcontinue.isEmpty() )
        url.addQueryItem( "llcontinue", llcontinue );

    q->setData( "wikipedia", "busy", "busy" );
    The::networkAccessManager()->getData( url, q,
         SLOT(_parseLangLinksResult(KUrl,QByteArray,NetworkAccessManagerProxy::Error)) );
}

void
WikipediaEnginePrivate::updateEngine()
{
    Q_Q( WikipediaEngine );

    if( !currentTrack )
        return;

    QString tmpWikiStr;
    switch( currentSelection )
    {
    case Artist:
        if( currentTrack->artist() )
        {
            if( currentTrack->artist()->name().isEmpty() )
            {
                debug() << "Requesting an empty string, skipping !";
                q->removeAllData( "wikipedia" );
                q->scheduleSourcesUpdated();
                q->setData( "wikipedia", "message", i18n( "No information found..." ) );
                return;
            }
            if( ( currentTrack->playableUrl().protocol() == "lastfm" ) ||
                ( currentTrack->playableUrl().protocol() == "daap" ) ||
                !The::engineController()->isStream() )
                tmpWikiStr = currentTrack->artist()->name();
            else
                tmpWikiStr = currentTrack->artist()->prettyName();
        }
        break;

    case Album:
        if( currentTrack->album() )
        {
            if( currentTrack->album()->name().isEmpty() )
            {
                debug() << "Requesting an empty string, skipping !";
                q->removeAllData( "wikipedia" );
                q->scheduleSourcesUpdated();
                q->setData( "wikipedia", "message", i18n( "No information found..." ) );
                return;
            }
            if( ( currentTrack->playableUrl().protocol() == "lastfm" ) ||
                ( currentTrack->playableUrl().protocol() == "daap" ) ||
                !The::engineController()->isStream() )
                tmpWikiStr = currentTrack->album()->name();

        }
        break;

    case Track:
        if( currentTrack->name().isEmpty() )
        {
            debug() << "Requesting an empty string, skipping !";
            q->removeAllData( "wikipedia" );
            q->scheduleSourcesUpdated();
            q->setData( "wikipedia", "message", i18n( "No information found..." ) );
            return;
        }
        tmpWikiStr = currentTrack->prettyName();
        break;
    }

    //Hack to make wiki searches work with magnatune preview tracks
    if( tmpWikiStr.contains( "PREVIEW: buy it at www.magnatune.com" ) )
    {
        tmpWikiStr = tmpWikiStr.remove(" (PREVIEW: buy it at www.magnatune.com)" );
        int index = tmpWikiStr.indexOf( '-' );
        if( index != -1 )
            tmpWikiStr = tmpWikiStr.left (index - 1);
    }

    if( preferredLangs.count() > 1 )
    {
        fetchLangLinks( tmpWikiStr );
    }
    else
    {
        const QStringList &prefixes = preferredLangs.first().split( QChar(':') );
        const QString &urlPrefix = (prefixes.count() == 1) ? prefixes.front() : prefixes.back();
        fetchWikiUrl( tmpWikiStr, urlPrefix );
    }
}

QString
WikipediaEnginePrivate::wikiParse()
{
    //remove the new-lines and tabs(replace with spaces IS needed).
    wiki.replace( '\n', ' ' );
    wiki.replace( '\t', ' ' );

    wikiLanguagesSection.clear();
    // Get the available language list
    if ( wiki.indexOf("<div id=\"p-lang\" class=\"portlet\">") != -1 )
    {
        wikiLanguagesSection = wiki.mid( wiki.indexOf("<div id=\"p-lang\" class=\"portlet\">") );
        wikiLanguagesSection = wikiLanguagesSection.mid( wikiLanguagesSection.indexOf("<ul>") );
        wikiLanguagesSection = wikiLanguagesSection.mid( 0, wikiLanguagesSection.indexOf( "</div>" ) );
    }

    QString copyright;
    QString copyrightMark = "<li id=\"f-copyright\">";
    if ( wiki.indexOf( copyrightMark ) != -1 )
    {
        copyright = wiki.mid( wiki.indexOf(copyrightMark) + copyrightMark.length() );
        copyright = copyright.mid( 0, copyright.indexOf( "</li>" ) );
        copyright.remove( "<br />" );
        //only one br at the beginning
        copyright.prepend( "<br />" );
    }

    // Ok lets remove the top and bottom parts of the page
    wiki = wiki.mid( wiki.indexOf( "<!-- start content -->" ) );
    wiki = wiki.mid( 0, wiki.indexOf( "<div class=\"printfooter\">" ) );

    // lets remove the warning box
    QString mbox = "<table class=\"metadata plainlinks ambox";
    QString mboxend = "</table>";
    while ( wiki.indexOf( mbox ) != -1 )
        wiki.remove( wiki.indexOf( mbox ), wiki.mid( wiki.indexOf( mbox ) ).indexOf( mboxend ) + mboxend.size() );

    QString protec = "<div><a href=\"/wiki/Wikipedia:Protection_policy" ;
    QString protecend = "</a></div>" ;
    while ( wiki.indexOf( protec ) != -1 )
        wiki.remove( wiki.indexOf( protec ), wiki.mid( wiki.indexOf( protec ) ).indexOf( protecend ) + protecend.size() );

    // lets also remove the "lock" image
    QString topicon = "<div class=\"metadata topicon\" " ;
    QString topiconend = "</a></div>";
     while ( wiki.indexOf( topicon ) != -1 )
        wiki.remove( wiki.indexOf( topicon ), wiki.mid( wiki.indexOf( topicon ) ).indexOf( topiconend ) + topiconend.size() );


    // Adding back style and license information
    wiki = "<div id=\"bodyContent\"" + wiki;
    wiki += copyright;
    wiki.append( "</div>" );
    wiki.remove( QRegExp("<h3 id=\"siteSub\">[^<]*</h3>") );

    wiki.remove( QRegExp( "<span class=\"editsection\"[^>]*>[^<]*<[^>]*>[^<]*<[^>]*>[^<]*</span>" ) );

    wiki.replace( QRegExp( "<a href=\"[^\"]*\" class=\"new\"[^>]*>([^<]*)</a>" ), "\\1" );

    // Remove anything inside of a class called urlexpansion, as it's pointless for us
    wiki.remove( QRegExp( "<span class= *'urlexpansion'>[^(]*[(][^)]*[)]</span>" ) );

    // Remove hidden table rows as well
    QRegExp hidden( "<tr *class= *[\"\']hiddenStructure[\"\']>.*</tr>", Qt::CaseInsensitive );
    hidden.setMinimal( true ); //greedy behaviour wouldn't be any good!
    wiki.remove( hidden );

    // we want to keep our own style (we need to modify the stylesheet a bit to handle things nicely)
    wiki.remove( QRegExp( "style= *\"[^\"]*\"" ) );
    // We need to leave the classes behind, otherwise styling it ourselves gets really nasty and tedious and roughly impossible to do in a sane maner
    //wiki.replace( QRegExp( "class= *\"[^\"]*\"" ), QString() );
    // let's remove the form elements, we don't want them.
    wiki.remove( QRegExp( "<input[^>]*>" ) );
    wiki.remove( QRegExp( "<select[^>]*>" ) );
    wiki.remove( "</select>\n"  );
    wiki.remove( QRegExp( "<option[^>]*>" ) );
    wiki.remove( "</option>\n"  );
    wiki.remove( QRegExp( "<textarea[^>]*>" ) );
    wiki.remove( "</textarea>" );

    QString html = "<html><body>\n";
    html.append( wiki );
    if( !wikiLanguagesSection.isEmpty() )
    {
        html.append( "<br/><div id=\"wiki_otherlangs\" >"
                     + i18nc( "@item:intext Wikipedia webview", "This article in other languages:") + "<br/>"
                     + wikiLanguagesSection + "</div>" );
    }
    html.append( "</body></html>\n" );
    return html;
}

void
WikipediaEnginePrivate::reloadWikipedia()
{
    DEBUG_BLOCK
    Q_Q( WikipediaEngine );
    debug() << "wiki url: " << wikiCurrentUrl;
    urls << wikiCurrentUrl;
    q->setData( "wikipedia", "busy", "busy" );
    q->scheduleSourcesUpdated();
    The::networkAccessManager()->getData( wikiCurrentUrl, q,
         SLOT(_wikiResult(KUrl,QByteArray,NetworkAccessManagerProxy::Error)) );
}

WikipediaEnginePrivate::SelectionType
WikipediaEnginePrivate::selection() const
{
    return currentSelection;
}

bool
WikipediaEnginePrivate::setSelection( SelectionType type )
{
    if( currentSelection != type )
    {
        currentSelection = type;
        return true;
    }
    return false;
}

bool
WikipediaEnginePrivate::setSelection( const QString &type )
{
    bool changed( false );
    if( type == "artist" )
        changed = setSelection( Artist );
    else if( type == "album" )
        changed = setSelection( Album );
    else if( type == "track" )
        changed = setSelection( Track );
    return changed;
}

WikipediaEngine::WikipediaEngine( QObject* parent, const QList<QVariant>& /*args*/ )
    : DataEngine( parent )
    , Engine::EngineObserver( The::engineController() )
    , d_ptr( new WikipediaEnginePrivate( this ) )
{
}

WikipediaEngine::~WikipediaEngine()
{
    delete d_ptr;
}

void
WikipediaEngine::init()
{
    Q_D( WikipediaEngine );
    d->dataContainer = new Plasma::DataContainer( this );
    d->dataContainer->setObjectName( "wikipedia" );
    addSource( d->dataContainer );
    connect( d->dataContainer, SIGNAL(dataUpdated(QString,Plasma::DataEngine::Data)),
             this, SLOT(_dataContainerUpdated(QString,Plasma::DataEngine::Data)) );
    d->currentTrack = The::engineController()->currentTrack();
    d->updateEngine();
}

bool
WikipediaEngine::sourceRequestEvent( const QString &source )
{
    if( source == "update" )
    {
        scheduleSourcesUpdated();
    }
    else if( source == "wikipedia" )
    {
        Q_D( WikipediaEngine );
        d->updateEngine();
        return true;
    }
    return false;
}

void
WikipediaEngine::engineTrackChanged( Meta::TrackPtr track )
{
    Q_D( WikipediaEngine );
    d->checkRequireUpdate( track );
}

void
WikipediaEngine::metadataChanged( Meta::TrackPtr track )
{
    Q_D( WikipediaEngine );
    d->checkRequireUpdate( track );
}

#include "WikipediaEngine.moc"

