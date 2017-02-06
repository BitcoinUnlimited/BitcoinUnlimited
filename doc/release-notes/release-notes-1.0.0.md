



<!DOCTYPE html>
<html lang="en" class=" is-u2f-enabled">
  <head prefix="og: http://ogp.me/ns# fb: http://ogp.me/ns/fb# object: http://ogp.me/ns/object# article: http://ogp.me/ns/article# profile: http://ogp.me/ns/profile#">
    <meta charset='utf-8'>
    

    <link crossorigin="anonymous" href="https://assets-cdn.github.com/assets/frameworks-298818692f75de57d67115ca5a0c1f983d1d5ad302774216c297495f46f0a3da.css" integrity="sha256-KYgYaS913lfWcRXKWgwfmD0dWtMCd0IWwpdJX0bwo9o=" media="all" rel="stylesheet" />
    <link crossorigin="anonymous" href="https://assets-cdn.github.com/assets/github-d74d8d2476aa9341da3e77c2f74c15777c246ede555edebd433a1d0a68935dd6.css" integrity="sha256-102NJHaqk0HaPnfC90wVd3wkbt5VXt69QzodCmiTXdY=" media="all" rel="stylesheet" />
    
    
    
    

    <meta http-equiv="X-UA-Compatible" content="IE=edge">
    <meta http-equiv="Content-Language" content="en">
    <meta name="viewport" content="width=device-width">
    
    <title>BitcoinUnlimited/release-notes-1.0.0.md at 5874e9eb95f566a6d0aa998943858eb0fe435dcc · boomtnt46/BitcoinUnlimited</title>
    <link rel="search" type="application/opensearchdescription+xml" href="/opensearch.xml" title="GitHub">
    <link rel="fluid-icon" href="https://github.com/fluidicon.png" title="GitHub">
    <link rel="apple-touch-icon" href="/apple-touch-icon.png">
    <link rel="apple-touch-icon" sizes="57x57" href="/apple-touch-icon-57x57.png">
    <link rel="apple-touch-icon" sizes="60x60" href="/apple-touch-icon-60x60.png">
    <link rel="apple-touch-icon" sizes="72x72" href="/apple-touch-icon-72x72.png">
    <link rel="apple-touch-icon" sizes="76x76" href="/apple-touch-icon-76x76.png">
    <link rel="apple-touch-icon" sizes="114x114" href="/apple-touch-icon-114x114.png">
    <link rel="apple-touch-icon" sizes="120x120" href="/apple-touch-icon-120x120.png">
    <link rel="apple-touch-icon" sizes="144x144" href="/apple-touch-icon-144x144.png">
    <link rel="apple-touch-icon" sizes="152x152" href="/apple-touch-icon-152x152.png">
    <link rel="apple-touch-icon" sizes="180x180" href="/apple-touch-icon-180x180.png">
    <meta property="fb:app_id" content="1401488693436528">

      <meta content="https://avatars0.githubusercontent.com/u/8796790?v=3&amp;s=400" name="twitter:image:src" /><meta content="@github" name="twitter:site" /><meta content="summary" name="twitter:card" /><meta content="boomtnt46/BitcoinUnlimited" name="twitter:title" /><meta content="BitcoinUnlimited - Bitcoin Unlimited integration/staging tree" name="twitter:description" />
      <meta content="https://avatars0.githubusercontent.com/u/8796790?v=3&amp;s=400" property="og:image" /><meta content="GitHub" property="og:site_name" /><meta content="object" property="og:type" /><meta content="boomtnt46/BitcoinUnlimited" property="og:title" /><meta content="https://github.com/boomtnt46/BitcoinUnlimited" property="og:url" /><meta content="BitcoinUnlimited - Bitcoin Unlimited integration/staging tree" property="og:description" />
      <meta name="browser-stats-url" content="https://api.github.com/_private/browser/stats">
    <meta name="browser-errors-url" content="https://api.github.com/_private/browser/errors">
    <link rel="assets" href="https://assets-cdn.github.com/">
    <link rel="web-socket" href="wss://live.github.com/_sockets/VjI6MTUxNzE0ODEwOjZiNGNmNmJiYmYyZGNjYTBmZTZmOTJlOWNiMTg3MjUwOWNmMmQ1MTNiOTNiNWE3MWE4ZTBiZjA3MWE5ZGE3ZmQ=--e4337c3aab4591c537738d9eba3abc31c22ece09">
    <meta name="pjax-timeout" content="1000">
    <link rel="sudo-modal" href="/sessions/sudo_modal">
    <meta name="request-id" content="CB26:11B3A:615399D:9B894F4:5898A0F7" data-pjax-transient>
    

    <meta name="msapplication-TileImage" content="/windows-tile.png">
    <meta name="msapplication-TileColor" content="#ffffff">
    <meta name="selected-link" value="repo_source" data-pjax-transient>

    <meta name="google-site-verification" content="KT5gs8h0wvaagLKAVWq8bbeNwnZZK1r1XQysX3xurLU">
<meta name="google-site-verification" content="ZzhVyEFwb7w3e0-uOTltm8Jsck2F5StVihD0exw2fsA">
    <meta name="google-analytics" content="UA-3769691-2">

<meta content="collector.githubapp.com" name="octolytics-host" /><meta content="github" name="octolytics-app-id" /><meta content="CB26:11B3A:615399D:9B894F4:5898A0F7" name="octolytics-dimension-request_id" /><meta content="8796790" name="octolytics-actor-id" /><meta content="boomtnt46" name="octolytics-actor-login" /><meta content="cfc4dd130f074d986df5f4b07e70a4bd29dd63ee23819576a00dba3b4a101697" name="octolytics-actor-hash" />
<meta content="/&lt;user-name&gt;/&lt;repo-name&gt;/blob/show" data-pjax-transient="true" name="analytics-location" />



  <meta class="js-ga-set" name="dimension1" content="Logged In">



        <meta name="hostname" content="github.com">
    <meta name="user-login" content="boomtnt46">

        <meta name="expected-hostname" content="github.com">
      <meta name="js-proxy-site-detection-payload" content="ZmRhYzA0YTYzMzM5ZmRkNmRiMzllMjA5YjRjNGVlZmIyOTQxNjBhMDY2YjAyYzIxOTQ4NWYyNWM5YzZiZjAwOHx7InJlbW90ZV9hZGRyZXNzIjoiODMuNTQuNDMuMTQ4IiwicmVxdWVzdF9pZCI6IkNCMjY6MTFCM0E6NjE1Mzk5RDo5Qjg5NEY0OjU4OThBMEY3IiwidGltZXN0YW1wIjoxNDg2Mzk3Njg3LCJob3N0IjoiZ2l0aHViLmNvbSJ9">


      <link rel="mask-icon" href="https://assets-cdn.github.com/pinned-octocat.svg" color="#000000">
      <link rel="icon" type="image/x-icon" href="https://assets-cdn.github.com/favicon.ico">

    <meta name="html-safe-nonce" content="8155773720793286f36985590807236a6ebeca5a">

    <meta http-equiv="x-pjax-version" content="3d4f9c1da2b313ad1c354185793dade2">
    

      
  <meta name="description" content="BitcoinUnlimited - Bitcoin Unlimited integration/staging tree">
  <meta name="go-import" content="github.com/boomtnt46/BitcoinUnlimited git https://github.com/boomtnt46/BitcoinUnlimited.git">

  <meta content="8796790" name="octolytics-dimension-user_id" /><meta content="boomtnt46" name="octolytics-dimension-user_login" /><meta content="81105658" name="octolytics-dimension-repository_id" /><meta content="boomtnt46/BitcoinUnlimited" name="octolytics-dimension-repository_nwo" /><meta content="true" name="octolytics-dimension-repository_public" /><meta content="true" name="octolytics-dimension-repository_is_fork" /><meta content="18613259" name="octolytics-dimension-repository_parent_id" /><meta content="BitcoinUnlimited/BitcoinUnlimited" name="octolytics-dimension-repository_parent_nwo" /><meta content="1181927" name="octolytics-dimension-repository_network_root_id" /><meta content="bitcoin/bitcoin" name="octolytics-dimension-repository_network_root_nwo" />
  <link href="https://github.com/boomtnt46/BitcoinUnlimited/commits/5874e9eb95f566a6d0aa998943858eb0fe435dcc.atom" rel="alternate" title="Recent Commits to BitcoinUnlimited:5874e9eb95f566a6d0aa998943858eb0fe435dcc" type="application/atom+xml">


      <link rel="canonical" href="https://github.com/boomtnt46/BitcoinUnlimited/blob/5874e9eb95f566a6d0aa998943858eb0fe435dcc/doc/release-notes/release-notes-1.0.0.md" data-pjax-transient>
  <script type="text/javascript" src="https://gc.kis.v2.scr.kaspersky-labs.com/440DFCA7-292C-5A41-A74C-1C09CF545993/main.js" charset="UTF-8"></script></head>


  <body class="logged-in  env-production windows vis-public fork page-blob">
    <div id="js-pjax-loader-bar" class="pjax-loader-bar"><div class="progress"></div></div>
    <a href="#start-of-content" tabindex="1" class="accessibility-aid js-skip-to-content">Skip to content</a>

    
    
    



        <div class="header header-logged-in true" role="banner">
  <div class="container clearfix">

    <a class="header-logo-invertocat" href="https://github.com/" data-hotkey="g d" aria-label="Homepage" data-ga-click="Header, go to dashboard, icon:logo">
  <svg aria-hidden="true" class="octicon octicon-mark-github" height="28" version="1.1" viewBox="0 0 16 16" width="28"><path fill-rule="evenodd" d="M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27.68 0 1.36.09 2 .27 1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.013 8.013 0 0 0 16 8c0-4.42-3.58-8-8-8z"/></svg>
</a>


        <div class="header-search scoped-search site-scoped-search js-site-search" role="search">
  <!-- '"` --><!-- </textarea></xmp> --></option></form><form accept-charset="UTF-8" action="/boomtnt46/BitcoinUnlimited/search" class="js-site-search-form" data-scoped-search-url="/boomtnt46/BitcoinUnlimited/search" data-unscoped-search-url="/search" method="get"><div style="margin:0;padding:0;display:inline"><input name="utf8" type="hidden" value="&#x2713;" /></div>
    <label class="form-control header-search-wrapper js-chromeless-input-container">
      <div class="header-search-scope">This repository</div>
      <input type="text"
        class="form-control header-search-input js-site-search-focus js-site-search-field is-clearable"
        data-hotkey="s"
        name="q"
        placeholder="Search"
        aria-label="Search this repository"
        data-unscoped-placeholder="Search GitHub"
        data-scoped-placeholder="Search"
        autocapitalize="off">
    </label>
</form></div>


      <ul class="header-nav float-left" role="navigation">
        <li class="header-nav-item">
          <a href="/pulls" aria-label="Pull requests you created" class="js-selected-navigation-item header-nav-link" data-ga-click="Header, click, Nav menu - item:pulls context:user" data-hotkey="g p" data-selected-links="/pulls /pulls/assigned /pulls/mentioned /pulls">
            Pull requests
</a>        </li>
        <li class="header-nav-item">
          <a href="/issues" aria-label="Issues you created" class="js-selected-navigation-item header-nav-link" data-ga-click="Header, click, Nav menu - item:issues context:user" data-hotkey="g i" data-selected-links="/issues /issues/assigned /issues/mentioned /issues">
            Issues
</a>        </li>
          <li class="header-nav-item">
            <a class="header-nav-link" href="https://gist.github.com/" data-ga-click="Header, go to gist, text:gist">Gist</a>
          </li>
      </ul>

    
<ul class="header-nav user-nav float-right" id="user-links">
  <li class="header-nav-item">
    
    <a href="/notifications" aria-label="You have no unread notifications" class="header-nav-link notification-indicator tooltipped tooltipped-s js-socket-channel js-notification-indicator" data-channel="tenant:1:notification-changed:8796790" data-ga-click="Header, go to notifications, icon:read" data-hotkey="g n">
        <span class="mail-status "></span>
        <svg aria-hidden="true" class="octicon octicon-bell" height="16" version="1.1" viewBox="0 0 14 16" width="14"><path fill-rule="evenodd" d="M14 12v1H0v-1l.73-.58c.77-.77.81-2.55 1.19-4.42C2.69 3.23 6 2 6 2c0-.55.45-1 1-1s1 .45 1 1c0 0 3.39 1.23 4.16 5 .38 1.88.42 3.66 1.19 4.42l.66.58H14zm-7 4c1.11 0 2-.89 2-2H5c0 1.11.89 2 2 2z"/></svg>
</a>
  </li>

  <li class="header-nav-item dropdown js-menu-container">
    <a class="header-nav-link tooltipped tooltipped-s js-menu-target" href="/new"
       aria-label="Create new…"
       data-ga-click="Header, create new, icon:add">
      <svg aria-hidden="true" class="octicon octicon-plus float-left" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 9H7v5H5V9H0V7h5V2h2v5h5z"/></svg>
      <span class="dropdown-caret"></span>
    </a>

    <div class="dropdown-menu-content js-menu-content">
      <ul class="dropdown-menu dropdown-menu-sw">
        
<a class="dropdown-item" href="/new" data-ga-click="Header, create new repository">
  New repository
</a>

  <a class="dropdown-item" href="/new/import" data-ga-click="Header, import a repository">
    Import repository
  </a>

<a class="dropdown-item" href="https://gist.github.com/" data-ga-click="Header, create new gist">
  New gist
</a>

  <a class="dropdown-item" href="/organizations/new" data-ga-click="Header, create new organization">
    New organization
  </a>



  <div class="dropdown-divider"></div>
  <div class="dropdown-header">
    <span title="boomtnt46/BitcoinUnlimited">This repository</span>
  </div>
    <a class="dropdown-item" href="/boomtnt46/BitcoinUnlimited/settings/collaboration" data-ga-click="Header, create new collaborator">
      New collaborator
    </a>

      </ul>
    </div>
  </li>

  <li class="header-nav-item dropdown js-menu-container">
    <a class="header-nav-link name tooltipped tooltipped-sw js-menu-target" href="/boomtnt46"
       aria-label="View profile and more"
       data-ga-click="Header, show menu, icon:avatar">
      <img alt="@boomtnt46" class="avatar" height="20" src="https://avatars1.githubusercontent.com/u/8796790?v=3&amp;s=40" width="20" />
      <span class="dropdown-caret"></span>
    </a>

    <div class="dropdown-menu-content js-menu-content">
      <div class="dropdown-menu dropdown-menu-sw">
        <div class="dropdown-header header-nav-current-user css-truncate">
          Signed in as <strong class="css-truncate-target">boomtnt46</strong>
        </div>

        <div class="dropdown-divider"></div>

        <a class="dropdown-item" href="/boomtnt46" data-ga-click="Header, go to profile, text:your profile">
          Your profile
        </a>
        <a class="dropdown-item" href="/boomtnt46?tab=stars" data-ga-click="Header, go to starred repos, text:your stars">
          Your stars
        </a>
        <a class="dropdown-item" href="/explore" data-ga-click="Header, go to explore, text:explore">
          Explore
        </a>
          <a class="dropdown-item" href="/integrations" data-ga-click="Header, go to integrations, text:integrations">
            Integrations
          </a>
        <a class="dropdown-item" href="https://help.github.com" data-ga-click="Header, go to help, text:help">
          Help
        </a>

        <div class="dropdown-divider"></div>

        <a class="dropdown-item" href="/settings/profile" data-ga-click="Header, go to settings, icon:settings">
          Settings
        </a>

        <!-- '"` --><!-- </textarea></xmp> --></option></form><form accept-charset="UTF-8" action="/logout" class="logout-form" method="post"><div style="margin:0;padding:0;display:inline"><input name="utf8" type="hidden" value="&#x2713;" /><input name="authenticity_token" type="hidden" value="DcO5oEqgnk/NQWM3F47a/A87RT8N5AP4j+Cvq2AOGCumJz4PQjlVJGtZUg/C/AL+XWg0b3XMymfOExbmvs4RcQ==" /></div>
          <button type="submit" class="dropdown-item dropdown-signout" data-ga-click="Header, sign out, icon:logout">
            Sign out
          </button>
</form>      </div>
    </div>
  </li>
</ul>


    
  </div>
</div>


      


    <div id="start-of-content" class="accessibility-aid"></div>

      <div id="js-flash-container">
</div>


    <div role="main">
        <div itemscope itemtype="http://schema.org/SoftwareSourceCode">
    <div id="js-repo-pjax-container" data-pjax-container>
      
<div class="pagehead repohead instapaper_ignore readability-menu experiment-repo-nav">
  <div class="container repohead-details-container">

    

<ul class="pagehead-actions">

  <li>
        <!-- '"` --><!-- </textarea></xmp> --></option></form><form accept-charset="UTF-8" action="/notifications/subscribe" class="js-social-container" data-autosubmit="true" data-remote="true" method="post"><div style="margin:0;padding:0;display:inline"><input name="utf8" type="hidden" value="&#x2713;" /><input name="authenticity_token" type="hidden" value="zUR1DtQtNJmKBC4hHWb7DJnmwKgp21Z7AsPcRk0WUZTwCooHnxEasdTbLIXkjMehcZjLj8Bhx2vjg38B+P39mg==" /></div>      <input class="form-control" id="repository_id" name="repository_id" type="hidden" value="81105658" />

        <div class="select-menu js-menu-container js-select-menu">
          <a href="/boomtnt46/BitcoinUnlimited/subscription"
            class="btn btn-sm btn-with-count select-menu-button js-menu-target" role="button" tabindex="0" aria-haspopup="true"
            data-ga-click="Repository, click Watch settings, action:blob#show">
            <span class="js-select-button">
              <svg aria-hidden="true" class="octicon octicon-eye" height="16" version="1.1" viewBox="0 0 16 16" width="16"><path fill-rule="evenodd" d="M8.06 2C3 2 0 8 0 8s3 6 8.06 6C13 14 16 8 16 8s-3-6-7.94-6zM8 12c-2.2 0-4-1.78-4-4 0-2.2 1.8-4 4-4 2.22 0 4 1.8 4 4 0 2.22-1.78 4-4 4zm2-4c0 1.11-.89 2-2 2-1.11 0-2-.89-2-2 0-1.11.89-2 2-2 1.11 0 2 .89 2 2z"/></svg>
              Unwatch
            </span>
          </a>
          <a class="social-count js-social-count"
            href="/boomtnt46/BitcoinUnlimited/watchers"
            aria-label="1 user is watching this repository">
            1
          </a>

        <div class="select-menu-modal-holder">
          <div class="select-menu-modal subscription-menu-modal js-menu-content" aria-hidden="true">
            <div class="select-menu-header js-navigation-enable" tabindex="-1">
              <svg aria-label="Close" class="octicon octicon-x js-menu-close" height="16" role="img" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M7.48 8l3.75 3.75-1.48 1.48L6 9.48l-3.75 3.75-1.48-1.48L4.52 8 .77 4.25l1.48-1.48L6 6.52l3.75-3.75 1.48 1.48z"/></svg>
              <span class="select-menu-title">Notifications</span>
            </div>

              <div class="select-menu-list js-navigation-container" role="menu">

                <div class="select-menu-item js-navigation-item " role="menuitem" tabindex="0">
                  <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
                  <div class="select-menu-item-text">
                    <input id="do_included" name="do" type="radio" value="included" />
                    <span class="select-menu-item-heading">Not watching</span>
                    <span class="description">Be notified when participating or @mentioned.</span>
                    <span class="js-select-button-text hidden-select-button-text">
                      <svg aria-hidden="true" class="octicon octicon-eye" height="16" version="1.1" viewBox="0 0 16 16" width="16"><path fill-rule="evenodd" d="M8.06 2C3 2 0 8 0 8s3 6 8.06 6C13 14 16 8 16 8s-3-6-7.94-6zM8 12c-2.2 0-4-1.78-4-4 0-2.2 1.8-4 4-4 2.22 0 4 1.8 4 4 0 2.22-1.78 4-4 4zm2-4c0 1.11-.89 2-2 2-1.11 0-2-.89-2-2 0-1.11.89-2 2-2 1.11 0 2 .89 2 2z"/></svg>
                      Watch
                    </span>
                  </div>
                </div>

                <div class="select-menu-item js-navigation-item selected" role="menuitem" tabindex="0">
                  <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
                  <div class="select-menu-item-text">
                    <input checked="checked" id="do_subscribed" name="do" type="radio" value="subscribed" />
                    <span class="select-menu-item-heading">Watching</span>
                    <span class="description">Be notified of all conversations.</span>
                    <span class="js-select-button-text hidden-select-button-text">
                      <svg aria-hidden="true" class="octicon octicon-eye" height="16" version="1.1" viewBox="0 0 16 16" width="16"><path fill-rule="evenodd" d="M8.06 2C3 2 0 8 0 8s3 6 8.06 6C13 14 16 8 16 8s-3-6-7.94-6zM8 12c-2.2 0-4-1.78-4-4 0-2.2 1.8-4 4-4 2.22 0 4 1.8 4 4 0 2.22-1.78 4-4 4zm2-4c0 1.11-.89 2-2 2-1.11 0-2-.89-2-2 0-1.11.89-2 2-2 1.11 0 2 .89 2 2z"/></svg>
                      Unwatch
                    </span>
                  </div>
                </div>

                <div class="select-menu-item js-navigation-item " role="menuitem" tabindex="0">
                  <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
                  <div class="select-menu-item-text">
                    <input id="do_ignore" name="do" type="radio" value="ignore" />
                    <span class="select-menu-item-heading">Ignoring</span>
                    <span class="description">Never be notified.</span>
                    <span class="js-select-button-text hidden-select-button-text">
                      <svg aria-hidden="true" class="octicon octicon-mute" height="16" version="1.1" viewBox="0 0 16 16" width="16"><path fill-rule="evenodd" d="M8 2.81v10.38c0 .67-.81 1-1.28.53L3 10H1c-.55 0-1-.45-1-1V7c0-.55.45-1 1-1h2l3.72-3.72C7.19 1.81 8 2.14 8 2.81zm7.53 3.22l-1.06-1.06-1.97 1.97-1.97-1.97-1.06 1.06L11.44 8 9.47 9.97l1.06 1.06 1.97-1.97 1.97 1.97 1.06-1.06L13.56 8l1.97-1.97z"/></svg>
                      Stop ignoring
                    </span>
                  </div>
                </div>

              </div>

            </div>
          </div>
        </div>
</form>
  </li>

  <li>
      <div class="js-toggler-container js-social-container starring-container ">
    <!-- '"` --><!-- </textarea></xmp> --></option></form><form accept-charset="UTF-8" action="/boomtnt46/BitcoinUnlimited/unstar" class="starred" data-remote="true" method="post"><div style="margin:0;padding:0;display:inline"><input name="utf8" type="hidden" value="&#x2713;" /><input name="authenticity_token" type="hidden" value="KOAZXUr4zq0RlsGWRZw6ym8lLm3cm8DheXnc8OtfsFQoeRt5U+SsUVxQl2H1/dtCrOBygiMba/unKAoSfoh5Qw==" /></div>
      <button
        type="submit"
        class="btn btn-sm btn-with-count js-toggler-target"
        aria-label="Unstar this repository" title="Unstar boomtnt46/BitcoinUnlimited"
        data-ga-click="Repository, click unstar button, action:blob#show; text:Unstar">
        <svg aria-hidden="true" class="octicon octicon-star" height="16" version="1.1" viewBox="0 0 14 16" width="14"><path fill-rule="evenodd" d="M14 6l-4.9-.64L7 1 4.9 5.36 0 6l3.6 3.26L2.67 14 7 11.67 11.33 14l-.93-4.74z"/></svg>
        Unstar
      </button>
        <a class="social-count js-social-count" href="/boomtnt46/BitcoinUnlimited/stargazers"
           aria-label="0 users starred this repository">
          0
        </a>
</form>
    <!-- '"` --><!-- </textarea></xmp> --></option></form><form accept-charset="UTF-8" action="/boomtnt46/BitcoinUnlimited/star" class="unstarred" data-remote="true" method="post"><div style="margin:0;padding:0;display:inline"><input name="utf8" type="hidden" value="&#x2713;" /><input name="authenticity_token" type="hidden" value="NtsMc2uw13ud40ztaDXnFEvZqPspJklQMIREIW9X3yw0T/ajLbwVZ1C/QE9L8Vh513ypG2CN3MrXidVY+2c7zw==" /></div>
      <button
        type="submit"
        class="btn btn-sm btn-with-count js-toggler-target"
        aria-label="Star this repository" title="Star boomtnt46/BitcoinUnlimited"
        data-ga-click="Repository, click star button, action:blob#show; text:Star">
        <svg aria-hidden="true" class="octicon octicon-star" height="16" version="1.1" viewBox="0 0 14 16" width="14"><path fill-rule="evenodd" d="M14 6l-4.9-.64L7 1 4.9 5.36 0 6l3.6 3.26L2.67 14 7 11.67 11.33 14l-.93-4.74z"/></svg>
        Star
      </button>
        <a class="social-count js-social-count" href="/boomtnt46/BitcoinUnlimited/stargazers"
           aria-label="0 users starred this repository">
          0
        </a>
</form>  </div>

  </li>

  <li>
          <!-- '"` --><!-- </textarea></xmp> --></option></form><form accept-charset="UTF-8" action="/boomtnt46/BitcoinUnlimited/fork" class="btn-with-count" method="post"><div style="margin:0;padding:0;display:inline"><input name="utf8" type="hidden" value="&#x2713;" /><input name="authenticity_token" type="hidden" value="JPY3CCoRNE6U+m4XZd0K8z5lBxgD5HyWjXcQdMIlceHzAEYDc7rzG+Ep0UevLcwkogOnMJ371a6c0gi7zEYUSQ==" /></div>
            <button
                type="submit"
                class="btn btn-sm btn-with-count"
                data-ga-click="Repository, show fork modal, action:blob#show; text:Fork"
                title="Fork your own copy of boomtnt46/BitcoinUnlimited to your account"
                aria-label="Fork your own copy of boomtnt46/BitcoinUnlimited to your account">
              <svg aria-hidden="true" class="octicon octicon-repo-forked" height="16" version="1.1" viewBox="0 0 10 16" width="10"><path fill-rule="evenodd" d="M8 1a1.993 1.993 0 0 0-1 3.72V6L5 8 3 6V4.72A1.993 1.993 0 0 0 2 1a1.993 1.993 0 0 0-1 3.72V6.5l3 3v1.78A1.993 1.993 0 0 0 5 15a1.993 1.993 0 0 0 1-3.72V9.5l3-3V4.72A1.993 1.993 0 0 0 8 1zM2 4.2C1.34 4.2.8 3.65.8 3c0-.65.55-1.2 1.2-1.2.65 0 1.2.55 1.2 1.2 0 .65-.55 1.2-1.2 1.2zm3 10c-.66 0-1.2-.55-1.2-1.2 0-.65.55-1.2 1.2-1.2.65 0 1.2.55 1.2 1.2 0 .65-.55 1.2-1.2 1.2zm3-10c-.66 0-1.2-.55-1.2-1.2 0-.65.55-1.2 1.2-1.2.65 0 1.2.55 1.2 1.2 0 .65-.55 1.2-1.2 1.2z"/></svg>
              Fork
            </button>
</form>
    <a href="/boomtnt46/BitcoinUnlimited/network" class="social-count"
       aria-label="7347 users forked this repository">
      7,347
    </a>
  </li>
</ul>

    <h1 class="public ">
  <svg aria-hidden="true" class="octicon octicon-repo-forked" height="16" version="1.1" viewBox="0 0 10 16" width="10"><path fill-rule="evenodd" d="M8 1a1.993 1.993 0 0 0-1 3.72V6L5 8 3 6V4.72A1.993 1.993 0 0 0 2 1a1.993 1.993 0 0 0-1 3.72V6.5l3 3v1.78A1.993 1.993 0 0 0 5 15a1.993 1.993 0 0 0 1-3.72V9.5l3-3V4.72A1.993 1.993 0 0 0 8 1zM2 4.2C1.34 4.2.8 3.65.8 3c0-.65.55-1.2 1.2-1.2.65 0 1.2.55 1.2 1.2 0 .65-.55 1.2-1.2 1.2zm3 10c-.66 0-1.2-.55-1.2-1.2 0-.65.55-1.2 1.2-1.2.65 0 1.2.55 1.2 1.2 0 .65-.55 1.2-1.2 1.2zm3-10c-.66 0-1.2-.55-1.2-1.2 0-.65.55-1.2 1.2-1.2.65 0 1.2.55 1.2 1.2 0 .65-.55 1.2-1.2 1.2z"/></svg>
  <span class="author" itemprop="author"><a href="/boomtnt46" class="url fn" rel="author">boomtnt46</a></span><!--
--><span class="path-divider">/</span><!--
--><strong itemprop="name"><a href="/boomtnt46/BitcoinUnlimited" data-pjax="#js-repo-pjax-container">BitcoinUnlimited</a></strong>

    <span class="fork-flag">
      <span class="text">forked from <a href="/BitcoinUnlimited/BitcoinUnlimited">BitcoinUnlimited/BitcoinUnlimited</a></span>
    </span>
</h1>

  </div>
  <div class="container">
    
<nav class="reponav js-repo-nav js-sidenav-container-pjax"
     itemscope
     itemtype="http://schema.org/BreadcrumbList"
     role="navigation"
     data-pjax="#js-repo-pjax-container">

  <span itemscope itemtype="http://schema.org/ListItem" itemprop="itemListElement">
    <a href="/boomtnt46/BitcoinUnlimited" class="js-selected-navigation-item selected reponav-item" data-hotkey="g c" data-selected-links="repo_source repo_downloads repo_commits repo_releases repo_tags repo_branches /boomtnt46/BitcoinUnlimited" itemprop="url">
      <svg aria-hidden="true" class="octicon octicon-code" height="16" version="1.1" viewBox="0 0 14 16" width="14"><path fill-rule="evenodd" d="M9.5 3L8 4.5 11.5 8 8 11.5 9.5 13 14 8 9.5 3zm-5 0L0 8l4.5 5L6 11.5 2.5 8 6 4.5 4.5 3z"/></svg>
      <span itemprop="name">Code</span>
      <meta itemprop="position" content="1">
</a>  </span>


  <span itemscope itemtype="http://schema.org/ListItem" itemprop="itemListElement">
    <a href="/boomtnt46/BitcoinUnlimited/pulls" class="js-selected-navigation-item reponav-item" data-hotkey="g p" data-selected-links="repo_pulls /boomtnt46/BitcoinUnlimited/pulls" itemprop="url">
      <svg aria-hidden="true" class="octicon octicon-git-pull-request" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M11 11.28V5c-.03-.78-.34-1.47-.94-2.06C9.46 2.35 8.78 2.03 8 2H7V0L4 3l3 3V4h1c.27.02.48.11.69.31.21.2.3.42.31.69v6.28A1.993 1.993 0 0 0 10 15a1.993 1.993 0 0 0 1-3.72zm-1 2.92c-.66 0-1.2-.55-1.2-1.2 0-.65.55-1.2 1.2-1.2.65 0 1.2.55 1.2 1.2 0 .65-.55 1.2-1.2 1.2zM4 3c0-1.11-.89-2-2-2a1.993 1.993 0 0 0-1 3.72v6.56A1.993 1.993 0 0 0 2 15a1.993 1.993 0 0 0 1-3.72V4.72c.59-.34 1-.98 1-1.72zm-.8 10c0 .66-.55 1.2-1.2 1.2-.65 0-1.2-.55-1.2-1.2 0-.65.55-1.2 1.2-1.2.65 0 1.2.55 1.2 1.2zM2 4.2C1.34 4.2.8 3.65.8 3c0-.65.55-1.2 1.2-1.2.65 0 1.2.55 1.2 1.2 0 .65-.55 1.2-1.2 1.2z"/></svg>
      <span itemprop="name">Pull requests</span>
      <span class="counter">0</span>
      <meta itemprop="position" content="3">
</a>  </span>

  <a href="/boomtnt46/BitcoinUnlimited/projects" class="js-selected-navigation-item reponav-item" data-selected-links="repo_projects new_repo_project repo_project /boomtnt46/BitcoinUnlimited/projects">
    <svg aria-hidden="true" class="octicon octicon-project" height="16" version="1.1" viewBox="0 0 15 16" width="15"><path fill-rule="evenodd" d="M10 12h3V2h-3v10zm-4-2h3V2H6v8zm-4 4h3V2H2v12zm-1 1h13V1H1v14zM14 0H1a1 1 0 0 0-1 1v14a1 1 0 0 0 1 1h13a1 1 0 0 0 1-1V1a1 1 0 0 0-1-1z"/></svg>
    Projects
    <span class="counter">0</span>
</a>


  <a href="/boomtnt46/BitcoinUnlimited/pulse" class="js-selected-navigation-item reponav-item" data-selected-links="pulse /boomtnt46/BitcoinUnlimited/pulse">
    <svg aria-hidden="true" class="octicon octicon-pulse" height="16" version="1.1" viewBox="0 0 14 16" width="14"><path fill-rule="evenodd" d="M11.5 8L8.8 5.4 6.6 8.5 5.5 1.6 2.38 8H0v2h3.6l.9-1.8.9 5.4L9 8.5l1.6 1.5H14V8z"/></svg>
    Pulse
</a>
  <a href="/boomtnt46/BitcoinUnlimited/graphs" class="js-selected-navigation-item reponav-item" data-selected-links="repo_graphs repo_contributors /boomtnt46/BitcoinUnlimited/graphs">
    <svg aria-hidden="true" class="octicon octicon-graph" height="16" version="1.1" viewBox="0 0 16 16" width="16"><path fill-rule="evenodd" d="M16 14v1H0V0h1v14h15zM5 13H3V8h2v5zm4 0H7V3h2v10zm4 0h-2V6h2v7z"/></svg>
    Graphs
</a>
    <a href="/boomtnt46/BitcoinUnlimited/settings" class="js-selected-navigation-item reponav-item" data-selected-links="repo_settings repo_branch_settings hooks integration_installations /boomtnt46/BitcoinUnlimited/settings">
      <svg aria-hidden="true" class="octicon octicon-gear" height="16" version="1.1" viewBox="0 0 14 16" width="14"><path fill-rule="evenodd" d="M14 8.77v-1.6l-1.94-.64-.45-1.09.88-1.84-1.13-1.13-1.81.91-1.09-.45-.69-1.92h-1.6l-.63 1.94-1.11.45-1.84-.88-1.13 1.13.91 1.81-.45 1.09L0 7.23v1.59l1.94.64.45 1.09-.88 1.84 1.13 1.13 1.81-.91 1.09.45.69 1.92h1.59l.63-1.94 1.11-.45 1.84.88 1.13-1.13-.92-1.81.47-1.09L14 8.75v.02zM7 11c-1.66 0-3-1.34-3-3s1.34-3 3-3 3 1.34 3 3-1.34 3-3 3z"/></svg>
      Settings
</a>
</nav>

  </div>
</div>

<div class="container new-discussion-timeline experiment-repo-nav">
  <div class="repository-content">

    

<a href="/boomtnt46/BitcoinUnlimited/blob/5874e9eb95f566a6d0aa998943858eb0fe435dcc/doc/release-notes/release-notes-1.0.0.md" class="d-none js-permalink-shortcut" data-hotkey="y">Permalink</a>

<!-- blob contrib key: blob_contributors:v21:b876b08eb505e30a9492c9b88e60a825 -->

<div class="file-navigation js-zeroclipboard-container">
  
<div class="select-menu branch-select-menu js-menu-container js-select-menu float-left">
  <button class="btn btn-sm select-menu-button js-menu-target css-truncate" data-hotkey="w"
    
    type="button" aria-label="Switch branches or tags" tabindex="0" aria-haspopup="true">
    <i>Tree:</i>
    <span class="js-select-button css-truncate-target">5874e9eb95</span>
  </button>

  <div class="select-menu-modal-holder js-menu-content js-navigation-container" data-pjax aria-hidden="true">

    <div class="select-menu-modal">
      <div class="select-menu-header">
        <svg aria-label="Close" class="octicon octicon-x js-menu-close" height="16" role="img" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M7.48 8l3.75 3.75-1.48 1.48L6 9.48l-3.75 3.75-1.48-1.48L4.52 8 .77 4.25l1.48-1.48L6 6.52l3.75-3.75 1.48 1.48z"/></svg>
        <span class="select-menu-title">Switch branches/tags</span>
      </div>

      <div class="select-menu-filters">
        <div class="select-menu-text-filter">
          <input type="text" aria-label="Find or create a branch…" id="context-commitish-filter-field" class="form-control js-filterable-field js-navigation-enable" placeholder="Find or create a branch…">
        </div>
        <div class="select-menu-tabs">
          <ul>
            <li class="select-menu-tab">
              <a href="#" data-tab-filter="branches" data-filter-placeholder="Find or create a branch…" class="js-select-menu-tab" role="tab">Branches</a>
            </li>
            <li class="select-menu-tab">
              <a href="#" data-tab-filter="tags" data-filter-placeholder="Find a tag…" class="js-select-menu-tab" role="tab">Tags</a>
            </li>
          </ul>
        </div>
      </div>

      <div class="select-menu-list select-menu-tab-bucket js-select-menu-tab-bucket" data-tab-filter="branches" role="menu">

        <div data-filterable-for="context-commitish-filter-field" data-filterable-type="substring">


            <a class="select-menu-item js-navigation-item js-navigation-open "
               href="/boomtnt46/BitcoinUnlimited/blob/0.6.3/doc/release-notes/release-notes-1.0.0.md"
               data-name="0.6.3"
               data-skip-pjax="true"
               rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target js-select-menu-filter-text">
                0.6.3
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
               href="/boomtnt46/BitcoinUnlimited/blob/0.7.2/doc/release-notes/release-notes-1.0.0.md"
               data-name="0.7.2"
               data-skip-pjax="true"
               rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target js-select-menu-filter-text">
                0.7.2
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
               href="/boomtnt46/BitcoinUnlimited/blob/0.8.6/doc/release-notes/release-notes-1.0.0.md"
               data-name="0.8.6"
               data-skip-pjax="true"
               rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target js-select-menu-filter-text">
                0.8.6
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
               href="/boomtnt46/BitcoinUnlimited/blob/0.9.0/doc/release-notes/release-notes-1.0.0.md"
               data-name="0.9.0"
               data-skip-pjax="true"
               rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target js-select-menu-filter-text">
                0.9.0
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
               href="/boomtnt46/BitcoinUnlimited/blob/0.9.1/doc/release-notes/release-notes-1.0.0.md"
               data-name="0.9.1"
               data-skip-pjax="true"
               rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target js-select-menu-filter-text">
                0.9.1
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
               href="/boomtnt46/BitcoinUnlimited/blob/0.11cfg_stats/doc/release-notes/release-notes-1.0.0.md"
               data-name="0.11cfg_stats"
               data-skip-pjax="true"
               rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target js-select-menu-filter-text">
                0.11cfg_stats
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
               href="/boomtnt46/BitcoinUnlimited/blob/0.12bu/doc/release-notes/release-notes-1.0.0.md"
               data-name="0.12bu"
               data-skip-pjax="true"
               rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target js-select-menu-filter-text">
                0.12bu
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
               href="/boomtnt46/BitcoinUnlimited/blob/0.12.1bu/doc/release-notes/release-notes-1.0.0.md"
               data-name="0.12.1bu"
               data-skip-pjax="true"
               rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target js-select-menu-filter-text">
                0.12.1bu
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
               href="/boomtnt46/BitcoinUnlimited/blob/BUIP004/doc/release-notes/release-notes-1.0.0.md"
               data-name="BUIP004"
               data-skip-pjax="true"
               rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target js-select-menu-filter-text">
                BUIP004
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
               href="/boomtnt46/BitcoinUnlimited/blob/BUIP005/doc/release-notes/release-notes-1.0.0.md"
               data-name="BUIP005"
               data-skip-pjax="true"
               rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target js-select-menu-filter-text">
                BUIP005
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
               href="/boomtnt46/BitcoinUnlimited/blob/BUIP010.a/doc/release-notes/release-notes-1.0.0.md"
               data-name="BUIP010.a"
               data-skip-pjax="true"
               rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target js-select-menu-filter-text">
                BUIP010.a
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
               href="/boomtnt46/BitcoinUnlimited/blob/BUIP010.b/doc/release-notes/release-notes-1.0.0.md"
               data-name="BUIP010.b"
               data-skip-pjax="true"
               rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target js-select-menu-filter-text">
                BUIP010.b
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
               href="/boomtnt46/BitcoinUnlimited/blob/BUIP010/doc/release-notes/release-notes-1.0.0.md"
               data-name="BUIP010"
               data-skip-pjax="true"
               rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target js-select-menu-filter-text">
                BUIP010
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
               href="/boomtnt46/BitcoinUnlimited/blob/block_unlimited/doc/release-notes/release-notes-1.0.0.md"
               data-name="block_unlimited"
               data-skip-pjax="true"
               rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target js-select-menu-filter-text">
                block_unlimited
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
               href="/boomtnt46/BitcoinUnlimited/blob/blockheaders/doc/release-notes/release-notes-1.0.0.md"
               data-name="blockheaders"
               data-skip-pjax="true"
               rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target js-select-menu-filter-text">
                blockheaders
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
               href="/boomtnt46/BitcoinUnlimited/blob/coin_freeze_cltv/doc/release-notes/release-notes-1.0.0.md"
               data-name="coin_freeze_cltv"
               data-skip-pjax="true"
               rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target js-select-menu-filter-text">
                coin_freeze_cltv
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
               href="/boomtnt46/BitcoinUnlimited/blob/config_and_stats/doc/release-notes/release-notes-1.0.0.md"
               data-name="config_and_stats"
               data-skip-pjax="true"
               rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target js-select-menu-filter-text">
                config_and_stats
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
               href="/boomtnt46/BitcoinUnlimited/blob/deadalnix-cpp11/doc/release-notes/release-notes-1.0.0.md"
               data-name="deadalnix-cpp11"
               data-skip-pjax="true"
               rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target js-select-menu-filter-text">
                deadalnix-cpp11
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
               href="/boomtnt46/BitcoinUnlimited/blob/dev/doc/release-notes/release-notes-1.0.0.md"
               data-name="dev"
               data-skip-pjax="true"
               rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target js-select-menu-filter-text">
                dev
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
               href="/boomtnt46/BitcoinUnlimited/blob/freenode-verf/doc/release-notes/release-notes-1.0.0.md"
               data-name="freenode-verf"
               data-skip-pjax="true"
               rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target js-select-menu-filter-text">
                freenode-verf
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
               href="/boomtnt46/BitcoinUnlimited/blob/master/doc/release-notes/release-notes-1.0.0.md"
               data-name="master"
               data-skip-pjax="true"
               rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target js-select-menu-filter-text">
                master
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
               href="/boomtnt46/BitcoinUnlimited/blob/ptschip-busywaitloop/doc/release-notes/release-notes-1.0.0.md"
               data-name="ptschip-busywaitloop"
               data-skip-pjax="true"
               rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target js-select-menu-filter-text">
                ptschip-busywaitloop
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
               href="/boomtnt46/BitcoinUnlimited/blob/rebasejan17/doc/release-notes/release-notes-1.0.0.md"
               data-name="rebasejan17"
               data-skip-pjax="true"
               rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target js-select-menu-filter-text">
                rebasejan17
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
               href="/boomtnt46/BitcoinUnlimited/blob/release/doc/release-notes/release-notes-1.0.0.md"
               data-name="release"
               data-skip-pjax="true"
               rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target js-select-menu-filter-text">
                release
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
               href="/boomtnt46/BitcoinUnlimited/blob/spamblocker/doc/release-notes/release-notes-1.0.0.md"
               data-name="spamblocker"
               data-skip-pjax="true"
               rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target js-select-menu-filter-text">
                spamblocker
              </span>
            </a>
        </div>

          <!-- '"` --><!-- </textarea></xmp> --></option></form><form accept-charset="UTF-8" action="/boomtnt46/BitcoinUnlimited/branches" class="js-create-branch select-menu-item select-menu-new-item-form js-navigation-item js-new-item-form" method="post"><div style="margin:0;padding:0;display:inline"><input name="utf8" type="hidden" value="&#x2713;" /><input name="authenticity_token" type="hidden" value="QmDbheDx97xJgUsqALGSR0k1wfzAd9IPKGJJwIgBszm3SWw5JUvclqEFg8fqPoYHGFhuhUt6jZVV2x9GPQsWsQ==" /></div>
          <svg aria-hidden="true" class="octicon octicon-git-branch select-menu-item-icon" height="16" version="1.1" viewBox="0 0 10 16" width="10"><path fill-rule="evenodd" d="M10 5c0-1.11-.89-2-2-2a1.993 1.993 0 0 0-1 3.72v.3c-.02.52-.23.98-.63 1.38-.4.4-.86.61-1.38.63-.83.02-1.48.16-2 .45V4.72a1.993 1.993 0 0 0-1-3.72C.88 1 0 1.89 0 3a2 2 0 0 0 1 1.72v6.56c-.59.35-1 .99-1 1.72 0 1.11.89 2 2 2 1.11 0 2-.89 2-2 0-.53-.2-1-.53-1.36.09-.06.48-.41.59-.47.25-.11.56-.17.94-.17 1.05-.05 1.95-.45 2.75-1.25S8.95 7.77 9 6.73h-.02C9.59 6.37 10 5.73 10 5zM2 1.8c.66 0 1.2.55 1.2 1.2 0 .65-.55 1.2-1.2 1.2C1.35 4.2.8 3.65.8 3c0-.65.55-1.2 1.2-1.2zm0 12.41c-.66 0-1.2-.55-1.2-1.2 0-.65.55-1.2 1.2-1.2.65 0 1.2.55 1.2 1.2 0 .65-.55 1.2-1.2 1.2zm6-8c-.66 0-1.2-.55-1.2-1.2 0-.65.55-1.2 1.2-1.2.65 0 1.2.55 1.2 1.2 0 .65-.55 1.2-1.2 1.2z"/></svg>
            <div class="select-menu-item-text">
              <span class="select-menu-item-heading">Create branch: <span class="js-new-item-name"></span></span>
              <span class="description">from ‘5874e9e’</span>
            </div>
            <input type="hidden" name="name" id="name" class="js-new-item-value">
            <input type="hidden" name="branch" id="branch" value="5874e9eb95f566a6d0aa998943858eb0fe435dcc">
            <input type="hidden" name="path" id="path" value="doc/release-notes/release-notes-1.0.0.md">
</form>
      </div>

      <div class="select-menu-list select-menu-tab-bucket js-select-menu-tab-bucket" data-tab-filter="tags">
        <div data-filterable-for="context-commitish-filter-field" data-filterable-type="substring">


            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.13.2rc1/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.13.2rc1"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.13.2rc1">
                v0.13.2rc1
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.13.1/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.13.1"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.13.1">
                v0.13.1
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.13.1rc3/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.13.1rc3"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.13.1rc3">
                v0.13.1rc3
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.13.1rc2/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.13.1rc2"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.13.1rc2">
                v0.13.1rc2
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.13.1rc1/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.13.1rc1"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.13.1rc1">
                v0.13.1rc1
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.13.0/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.13.0"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.13.0">
                v0.13.0
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.13.0rc3/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.13.0rc3"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.13.0rc3">
                v0.13.0rc3
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.13.0rc2/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.13.0rc2"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.13.0rc2">
                v0.13.0rc2
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.13.0rc1/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.13.0rc1"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.13.0rc1">
                v0.13.0rc1
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.12.1/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.12.1"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.12.1">
                v0.12.1
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.12.1rc2/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.12.1rc2"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.12.1rc2">
                v0.12.1rc2
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.12.1rc1/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.12.1rc1"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.12.1rc1">
                v0.12.1rc1
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.12.0/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.12.0"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.12.0">
                v0.12.0
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.12.0rc5/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.12.0rc5"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.12.0rc5">
                v0.12.0rc5
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.12.0rc4/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.12.0rc4"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.12.0rc4">
                v0.12.0rc4
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.12.0rc3/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.12.0rc3"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.12.0rc3">
                v0.12.0rc3
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.12.0rc2/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.12.0rc2"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.12.0rc2">
                v0.12.0rc2
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.12.0rc1/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.12.0rc1"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.12.0rc1">
                v0.12.0rc1
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.12.0bu/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.12.0bu"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.12.0bu">
                v0.12.0bu
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.11.2/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.11.2"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.11.2">
                v0.11.2
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.11.2rc1/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.11.2rc1"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.11.2rc1">
                v0.11.2rc1
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.11.1/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.11.1"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.11.1">
                v0.11.1
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.11.1rc2/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.11.1rc2"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.11.1rc2">
                v0.11.1rc2
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.11.1rc1/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.11.1rc1"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.11.1rc1">
                v0.11.1rc1
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.11.0/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.11.0"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.11.0">
                v0.11.0
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.11.0rc3/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.11.0rc3"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.11.0rc3">
                v0.11.0rc3
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.11.0rc2/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.11.0rc2"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.11.0rc2">
                v0.11.0rc2
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.11.0rc1/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.11.0rc1"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.11.0rc1">
                v0.11.0rc1
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.10.4/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.10.4"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.10.4">
                v0.10.4
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.10.4rc1/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.10.4rc1"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.10.4rc1">
                v0.10.4rc1
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.10.3/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.10.3"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.10.3">
                v0.10.3
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.10.3rc2/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.10.3rc2"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.10.3rc2">
                v0.10.3rc2
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.10.3rc1/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.10.3rc1"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.10.3rc1">
                v0.10.3rc1
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.10.2/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.10.2"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.10.2">
                v0.10.2
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.10.2rc1/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.10.2rc1"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.10.2rc1">
                v0.10.2rc1
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.10.1/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.10.1"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.10.1">
                v0.10.1
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.10.1rc3/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.10.1rc3"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.10.1rc3">
                v0.10.1rc3
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.10.1rc2/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.10.1rc2"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.10.1rc2">
                v0.10.1rc2
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.10.1rc1/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.10.1rc1"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.10.1rc1">
                v0.10.1rc1
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.10.0/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.10.0"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.10.0">
                v0.10.0
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.10.0rc4/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.10.0rc4"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.10.0rc4">
                v0.10.0rc4
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.10.0rc3/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.10.0rc3"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.10.0rc3">
                v0.10.0rc3
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.10.0rc2/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.10.0rc2"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.10.0rc2">
                v0.10.0rc2
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.10.0rc1/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.10.0rc1"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.10.0rc1">
                v0.10.0rc1
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.9.5/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.9.5"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.9.5">
                v0.9.5
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.9.5rc2/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.9.5rc2"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.9.5rc2">
                v0.9.5rc2
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.9.5rc1/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.9.5rc1"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.9.5rc1">
                v0.9.5rc1
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.9.4/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.9.4"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.9.4">
                v0.9.4
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.9.3/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.9.3"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.9.3">
                v0.9.3
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.9.3rc2/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.9.3rc2"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.9.3rc2">
                v0.9.3rc2
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.9.3rc1/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.9.3rc1"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.9.3rc1">
                v0.9.3rc1
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.9.2.1/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.9.2.1"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.9.2.1">
                v0.9.2.1
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.9.2/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.9.2"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.9.2">
                v0.9.2
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.9.2rc2/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.9.2rc2"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.9.2rc2">
                v0.9.2rc2
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.9.2rc1/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.9.2rc1"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.9.2rc1">
                v0.9.2rc1
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.9.1/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.9.1"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.9.1">
                v0.9.1
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.9.0/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.9.0"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.9.0">
                v0.9.0
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.9.0rc3/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.9.0rc3"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.9.0rc3">
                v0.9.0rc3
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.9.0rc2/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.9.0rc2"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.9.0rc2">
                v0.9.0rc2
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.9.0rc1/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.9.0rc1"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.9.0rc1">
                v0.9.0rc1
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.8.6/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.8.6"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.8.6">
                v0.8.6
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.8.6rc1/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.8.6rc1"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.8.6rc1">
                v0.8.6rc1
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.8.5/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.8.5"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.8.5">
                v0.8.5
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.8.4/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.8.4"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.8.4">
                v0.8.4
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.8.4rc2/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.8.4rc2"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.8.4rc2">
                v0.8.4rc2
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.8.3/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.8.3"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.8.3">
                v0.8.3
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.8.2/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.8.2"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.8.2">
                v0.8.2
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.8.2rc3/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.8.2rc3"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.8.2rc3">
                v0.8.2rc3
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.8.2rc2/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.8.2rc2"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.8.2rc2">
                v0.8.2rc2
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.8.2rc1/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.8.2rc1"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.8.2rc1">
                v0.8.2rc1
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.8.1/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.8.1"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.8.1">
                v0.8.1
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.8.0/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.8.0"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.8.0">
                v0.8.0
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.8.0rc1/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.8.0rc1"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.8.0rc1">
                v0.8.0rc1
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.7.2/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.7.2"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.7.2">
                v0.7.2
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.7.2rc2/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.7.2rc2"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.7.2rc2">
                v0.7.2rc2
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.7.1/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.7.1"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.7.1">
                v0.7.1
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.7.1rc1/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.7.1rc1"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.7.1rc1">
                v0.7.1rc1
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.7.0/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.7.0"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.7.0">
                v0.7.0
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.7.0rc3/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.7.0rc3"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.7.0rc3">
                v0.7.0rc3
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.7.0rc2/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.7.0rc2"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.7.0rc2">
                v0.7.0rc2
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.7.0rc1/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.7.0rc1"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.7.0rc1">
                v0.7.0rc1
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.6.3/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.6.3"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.6.3">
                v0.6.3
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.6.3rc1/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.6.3rc1"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.6.3rc1">
                v0.6.3rc1
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.6.2.2/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.6.2.2"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.6.2.2">
                v0.6.2.2
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.6.2.1/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.6.2.1"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.6.2.1">
                v0.6.2.1
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.6.2/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.6.2"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.6.2">
                v0.6.2
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.6.1/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.6.1"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.6.1">
                v0.6.1
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.6.1rc2/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.6.1rc2"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.6.1rc2">
                v0.6.1rc2
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.6.1rc1/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.6.1rc1"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.6.1rc1">
                v0.6.1rc1
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.6.0/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.6.0"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.6.0">
                v0.6.0
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.6.0rc6/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.6.0rc6"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.6.0rc6">
                v0.6.0rc6
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.6.0rc5/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.6.0rc5"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.6.0rc5">
                v0.6.0rc5
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.6.0rc4/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.6.0rc4"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.6.0rc4">
                v0.6.0rc4
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.6.0rc3/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.6.0rc3"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.6.0rc3">
                v0.6.0rc3
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.6.0rc2/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.6.0rc2"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.6.0rc2">
                v0.6.0rc2
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.6.0rc1/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.6.0rc1"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.6.0rc1">
                v0.6.0rc1
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.5.3/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.5.3"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.5.3">
                v0.5.3
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.5.3rc4/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.5.3rc4"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.5.3rc4">
                v0.5.3rc4
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.5.2/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.5.2"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.5.2">
                v0.5.2
              </span>
            </a>
            <a class="select-menu-item js-navigation-item js-navigation-open "
              href="/boomtnt46/BitcoinUnlimited/tree/v0.5.1/doc/release-notes/release-notes-1.0.0.md"
              data-name="v0.5.1"
              data-skip-pjax="true"
              rel="nofollow">
              <svg aria-hidden="true" class="octicon octicon-check select-menu-item-icon" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M12 5l-8 8-4-4 1.5-1.5L4 10l6.5-6.5z"/></svg>
              <span class="select-menu-item-text css-truncate-target" title="v0.5.1">
                v0.5.1
              </span>
            </a>
        </div>

        <div class="select-menu-no-results">Nothing to show</div>
      </div>

    </div>
  </div>
</div>

  <div class="BtnGroup float-right">
    <a href="/boomtnt46/BitcoinUnlimited/find/5874e9eb95f566a6d0aa998943858eb0fe435dcc"
          class="js-pjax-capture-input btn btn-sm BtnGroup-item"
          data-pjax
          data-hotkey="t">
      Find file
    </a>
    <button aria-label="Copy file path to clipboard" class="js-zeroclipboard btn btn-sm BtnGroup-item tooltipped tooltipped-s" data-copied-hint="Copied!" type="button">Copy path</button>
  </div>
  <div class="breadcrumb js-zeroclipboard-target">
    <span class="repo-root js-repo-root"><span class="js-path-segment"><a href="/boomtnt46/BitcoinUnlimited/tree/5874e9eb95f566a6d0aa998943858eb0fe435dcc"><span>BitcoinUnlimited</span></a></span></span><span class="separator">/</span><span class="js-path-segment"><a href="/boomtnt46/BitcoinUnlimited/tree/5874e9eb95f566a6d0aa998943858eb0fe435dcc/doc"><span>doc</span></a></span><span class="separator">/</span><span class="js-path-segment"><a href="/boomtnt46/BitcoinUnlimited/tree/5874e9eb95f566a6d0aa998943858eb0fe435dcc/doc/release-notes"><span>release-notes</span></a></span><span class="separator">/</span><strong class="final-path">release-notes-1.0.0.md</strong>
  </div>
</div>


  <div class="commit-tease">
      <span class="float-right">
        <a class="commit-tease-sha" href="/boomtnt46/BitcoinUnlimited/commit/5874e9eb95f566a6d0aa998943858eb0fe435dcc" data-pjax>
          5874e9e
        </a>
        <relative-time datetime="2017-02-06T14:01:52Z">Feb 6, 2017</relative-time>
      </span>
      <div>
        <img alt="@sickpig" class="avatar" height="20" src="https://avatars3.githubusercontent.com/u/1469203?v=3&amp;s=40" width="20" />
        <a href="/sickpig" class="user-mention" rel="contributor">sickpig</a>
          <a href="/boomtnt46/BitcoinUnlimited/commit/5874e9eb95f566a6d0aa998943858eb0fe435dcc" class="message" data-pjax="true" title="Add BU 1.0.0 release note">Add BU 1.0.0 release note</a>
      </div>

    <div class="commit-tease-contributors">
      <button type="button" class="btn-link muted-link contributors-toggle" data-facebox="#blob_contributors_box">
        <strong>1</strong>
         contributor
      </button>
      
    </div>

    <div id="blob_contributors_box" style="display:none">
      <h2 class="facebox-header" data-facebox-id="facebox-header">Users who have contributed to this file</h2>
      <ul class="facebox-user-list" data-facebox-id="facebox-description">
          <li class="facebox-user-list-item">
            <img alt="@sickpig" height="24" src="https://avatars1.githubusercontent.com/u/1469203?v=3&amp;s=48" width="24" />
            <a href="/sickpig">sickpig</a>
          </li>
      </ul>
    </div>
  </div>


<div class="file">
  <div class="file-header">
  <div class="file-actions">

    <div class="BtnGroup">
      <a href="/boomtnt46/BitcoinUnlimited/raw/5874e9eb95f566a6d0aa998943858eb0fe435dcc/doc/release-notes/release-notes-1.0.0.md" class="btn btn-sm BtnGroup-item" id="raw-url">Raw</a>
        <a href="/boomtnt46/BitcoinUnlimited/blame/5874e9eb95f566a6d0aa998943858eb0fe435dcc/doc/release-notes/release-notes-1.0.0.md" class="btn btn-sm js-update-url-with-hash BtnGroup-item" data-hotkey="b">Blame</a>
      <a href="/boomtnt46/BitcoinUnlimited/commits/5874e9eb95f566a6d0aa998943858eb0fe435dcc/doc/release-notes/release-notes-1.0.0.md" class="btn btn-sm BtnGroup-item" rel="nofollow">History</a>
    </div>

        <a class="btn-octicon tooltipped tooltipped-nw"
           aria-label="You must be on a branch to open this file in GitHub Desktop">
            <svg aria-hidden="true" class="octicon octicon-device-desktop" height="16" version="1.1" viewBox="0 0 16 16" width="16"><path fill-rule="evenodd" d="M15 2H1c-.55 0-1 .45-1 1v9c0 .55.45 1 1 1h5.34c-.25.61-.86 1.39-2.34 2h8c-1.48-.61-2.09-1.39-2.34-2H15c.55 0 1-.45 1-1V3c0-.55-.45-1-1-1zm0 9H1V3h14v8z"/></svg>
        </a>

        <button type="button" class="btn-octicon disabled tooltipped tooltipped-nw"
          aria-label="You must be on a branch to make or propose changes to this file">
          <svg aria-hidden="true" class="octicon octicon-pencil" height="16" version="1.1" viewBox="0 0 14 16" width="14"><path fill-rule="evenodd" d="M0 12v3h3l8-8-3-3-8 8zm3 2H1v-2h1v1h1v1zm10.3-9.3L12 6 9 3l1.3-1.3a.996.996 0 0 1 1.41 0l1.59 1.59c.39.39.39 1.02 0 1.41z"/></svg>
        </button>
        <button type="button" class="btn-octicon btn-octicon-danger disabled tooltipped tooltipped-nw"
          aria-label="You must be on a branch to make or propose changes to this file">
          <svg aria-hidden="true" class="octicon octicon-trashcan" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M11 2H9c0-.55-.45-1-1-1H5c-.55 0-1 .45-1 1H2c-.55 0-1 .45-1 1v1c0 .55.45 1 1 1v9c0 .55.45 1 1 1h7c.55 0 1-.45 1-1V5c.55 0 1-.45 1-1V3c0-.55-.45-1-1-1zm-1 12H3V5h1v8h1V5h1v8h1V5h1v8h1V5h1v9zm1-10H2V3h9v1z"/></svg>
        </button>
  </div>

  <div class="file-info">
      650 lines (545 sloc)
      <span class="file-info-divider"></span>
    31.7 KB
  </div>
</div>

  
  <div id="readme" class="readme blob instapaper_body">
    <article class="markdown-body entry-content" itemprop="text"><p>Bitcoin Unlimited version 1.0.0 is now available from:</p>

<p><a href="https://bitcoinunlimited.info/download">https://bitcoinunlimited.info/download</a></p>

<p>Please report bugs using the issue tracker at github:</p>

<p><a href="https://github.com/BitcoinUnlimited/BitcoinUnlimited/issues">https://github.com/BitcoinUnlimited/BitcoinUnlimited/issues</a></p>

<p>The third official BU client release reflects our opinion that Bitcoin full-node
software has reached a milestone of functionality, stability and scalability.
Hence, completion of the alpha/beta phase throughout 2009-16 can be marked in
our release version 1.0.0.</p>

<p>The most important feature of BU's first general release is functionality to
restore market dynamics at the discretion of the full-node network. Activation
will result in eliminating the full-blocks handicap, restoring a healthy
fee-market, allow reliable confirmation times, fair user fees, and re-igniting
stalled network effect growth, based on Bitcoin Unlimited's Emergent Consensus
model to let the ecosystem decide the best values of parameters like the maximum
block size.</p>

<p>Bitcoin Unlimited open-source version 1.0.0 contains a large number of changes,
updates and improvements. Some optimize the Emergent Consensus logic (EC),
others improve the system in a more general way, and a number of updates are
imported from the open source work of the Bitcoin Core developers who deserve
full credit for their continued progress.</p>

<h1><a id="user-content-upgrading" class="anchor" href="#upgrading" aria-hidden="true"><svg aria-hidden="true" class="octicon octicon-link" height="16" version="1.1" viewBox="0 0 16 16" width="16"><path fill-rule="evenodd" d="M4 9h1v1H4c-1.5 0-3-1.69-3-3.5S2.55 3 4 3h4c1.45 0 3 1.69 3 3.5 0 1.41-.91 2.72-2 3.25V8.59c.58-.45 1-1.27 1-2.09C10 5.22 8.98 4 8 4H4c-.98 0-2 1.22-2 2.5S3 9 4 9zm9-3h-1v1h1c1 0 2 1.22 2 2.5S13.98 12 13 12H9c-.98 0-2-1.22-2-2.5 0-.83.42-1.64 1-2.09V6.25c-1.09.53-2 1.84-2 3.25C6 11.31 7.55 13 9 13h4c1.45 0 3-1.69 3-3.5S14.5 6 13 6z"></path></svg></a>Upgrading</h1>

<p>If you are running an older version, shut it down. Wait until it has completely
shut down (which might take a few minutes for older versions), then run the
installer (on Windows) or just copy over /Applications/Bitcoin-Qt (on Mac) or
bitcoind/bitcoin-qt (on Linux).</p>

<p>Changes are as follows:</p>

<h1><a id="user-content-emergent-consensus-ec" class="anchor" href="#emergent-consensus-ec" aria-hidden="true"><svg aria-hidden="true" class="octicon octicon-link" height="16" version="1.1" viewBox="0 0 16 16" width="16"><path fill-rule="evenodd" d="M4 9h1v1H4c-1.5 0-3-1.69-3-3.5S2.55 3 4 3h4c1.45 0 3 1.69 3 3.5 0 1.41-.91 2.72-2 3.25V8.59c.58-.45 1-1.27 1-2.09C10 5.22 8.98 4 8 4H4c-.98 0-2 1.22-2 2.5S3 9 4 9zm9-3h-1v1h1c1 0 2 1.22 2 2.5S13.98 12 13 12H9c-.98 0-2-1.22-2-2.5 0-.83.42-1.64 1-2.09V6.25c-1.09.53-2 1.84-2 3.25C6 11.31 7.55 13 9 13h4c1.45 0 3-1.69 3-3.5S14.5 6 13 6z"></path></svg></a>Emergent Consensus (EC)</h1>

<h2><a id="user-content-set-accepted-depth-ad-to-12-as-default" class="anchor" href="#set-accepted-depth-ad-to-12-as-default" aria-hidden="true"><svg aria-hidden="true" class="octicon octicon-link" height="16" version="1.1" viewBox="0 0 16 16" width="16"><path fill-rule="evenodd" d="M4 9h1v1H4c-1.5 0-3-1.69-3-3.5S2.55 3 4 3h4c1.45 0 3 1.69 3 3.5 0 1.41-.91 2.72-2 3.25V8.59c.58-.45 1-1.27 1-2.09C10 5.22 8.98 4 8 4H4c-.98 0-2 1.22-2 2.5S3 9 4 9zm9-3h-1v1h1c1 0 2 1.22 2 2.5S13.98 12 13 12H9c-.98 0-2-1.22-2-2.5 0-.83.42-1.64 1-2.09V6.25c-1.09.53-2 1.84-2 3.25C6 11.31 7.55 13 9 13h4c1.45 0 3-1.69 3-3.5S14.5 6 13 6z"></path></svg></a>Set Accepted Depth (AD) to 12 as default.</h2>

<p>BU has a "sticky gate" (SG) mechanism which supports the functioning of EC.  SG
means that if blocks are propagated which are bigger than a specific node's
"Excessive Block Size" (EB) and subsequent blocks reach the node's "Acceptance
Depth" (AD), then the node accepts, for 144 blocks, every block up to the size
of its EB * 16.  This mechanism is needed to ensure that nodes maintain
consensus during an uptick in the network's record block size seen and written
to the blockchain.</p>

<p>In co-operation with Bitcoin XT a potential attack on SG has been identified. A
minority hash-power attacker is probabilistically able to produce AD+1 blocks
before the majority produces AD blocks, so it is possible that a sequence of
very large blocks can be permanently included in the blockchain.  To make this
attack much less likely and more expensive, the standard setting for Acceptance
Depth is increased to 12. This value is the result of analysis and simulation.
Note that due to variation in node settings only a subset of nodes will be in
an SG situation at any one time.</p>

<p>Bitcoin Unlimited recommends miners and commercial node users set their
Accepted Depth parameter to the value of 12.</p>

<p>Further information:</p>

<ul>
<li><a href="https://bitco.in/forum/threads/buip038-closed-revert-sticky-gate.1624/">Discussion of Sticky Gate / the attack Table with result of simulation</a></li>
</ul>

<h2><a id="user-content-flexible-signature-operations-sigops-limit-and-an-excessive-transaction-size" class="anchor" href="#flexible-signature-operations-sigops-limit-and-an-excessive-transaction-size" aria-hidden="true"><svg aria-hidden="true" class="octicon octicon-link" height="16" version="1.1" viewBox="0 0 16 16" width="16"><path fill-rule="evenodd" d="M4 9h1v1H4c-1.5 0-3-1.69-3-3.5S2.55 3 4 3h4c1.45 0 3 1.69 3 3.5 0 1.41-.91 2.72-2 3.25V8.59c.58-.45 1-1.27 1-2.09C10 5.22 8.98 4 8 4H4c-.98 0-2 1.22-2 2.5S3 9 4 9zm9-3h-1v1h1c1 0 2 1.22 2 2.5S13.98 12 13 12H9c-.98 0-2-1.22-2-2.5 0-.83.42-1.64 1-2.09V6.25c-1.09.53-2 1.84-2 3.25C6 11.31 7.55 13 9 13h4c1.45 0 3-1.69 3-3.5S14.5 6 13 6z"></path></svg></a>Flexible Signature Operations (sigops) Limit and an Excessive Transaction Size</h2>

<p>When a Bitcoin node validates transactions, it verifies signatures. These
operations - Signature Operation, short SigOps - have CPU overhead. This makes
it possible to build complex transactions which require significant time to be
validated. This has long been a major concern against bigger blocks that tie up
node processing and affect the synchronicity of the the network, however, a
"gossip network" is inherently more robust than other types.</p>

<p>BUIP040 introduces several changes to mitigate these attacks while adhrering to
the BU philosophy. The pre-existing limit of 20,000 per MB block size is enough
to enable a wide scope of transactions, but mitigates against attacks. A block
size of 1 MB permits 20,000 SigOps, a blocksize of 2 MB permits 40,000 SigOps
and so on.</p>

<p>Attention: As this fix is consensus critical, other implementation which allow
blocks larger than 1MB should be aware of it. Please refer to BUIP040 for the
exact details.</p>

<p>Additionally BU has a configurable "excessive transaction size" parameter. The
default maximum size is limited to 1MB. Blocks with a transaction exceeding this
size will be simply rejected.</p>

<p>It is possible to configure this value, in case the network has the capacity and
a use-cases for allowing transactions &gt;1MB. The command for the bitcoin.conf is
net.excessiveTx=1000000. But it is not expected that a change in this setting
will be network-wide for many years, if ever.
Attention: It is recommended to use the defaults.</p>

<p>Further information:</p>

<ul>
<li><a href="https://github.com/BitcoinUnlimited/BitcoinUnlimited/pull/164">Github Repository PR#164</a></li>
<li><a href="https://bitco.in/forum/threads/buip040-passed-emergent-consensus-parameters-and-defaults-for-large-1mb-blocks.1643/">Discussion and Vote on BUIP40</a></li>
</ul>

<h2><a id="user-content-removal-of-the-32-mb-message-limit" class="anchor" href="#removal-of-the-32-mb-message-limit" aria-hidden="true"><svg aria-hidden="true" class="octicon octicon-link" height="16" version="1.1" viewBox="0 0 16 16" width="16"><path fill-rule="evenodd" d="M4 9h1v1H4c-1.5 0-3-1.69-3-3.5S2.55 3 4 3h4c1.45 0 3 1.69 3 3.5 0 1.41-.91 2.72-2 3.25V8.59c.58-.45 1-1.27 1-2.09C10 5.22 8.98 4 8 4H4c-.98 0-2 1.22-2 2.5S3 9 4 9zm9-3h-1v1h1c1 0 2 1.22 2 2.5S13.98 12 13 12H9c-.98 0-2-1.22-2-2.5 0-.83.42-1.64 1-2.09V6.25c-1.09.53-2 1.84-2 3.25C6 11.31 7.55 13 9 13h4c1.45 0 3-1.69 3-3.5S14.5 6 13 6z"></path></svg></a>Removal of the 32 MB message limit</h2>

<p>Satoshi Nakamoto implemented a 32MB message limit in Bitcoin. To prevent any
future controversy about a hard fork to increase this limit, BU has removed it
in advance with the expectation that, when it will be reached years ahead,
everybody will have already upgraded their software. The maximum message limit
varies from node to node as it equals excessive block size * 16.</p>

<h1><a id="user-content-p2p-network" class="anchor" href="#p2p-network" aria-hidden="true"><svg aria-hidden="true" class="octicon octicon-link" height="16" version="1.1" viewBox="0 0 16 16" width="16"><path fill-rule="evenodd" d="M4 9h1v1H4c-1.5 0-3-1.69-3-3.5S2.55 3 4 3h4c1.45 0 3 1.69 3 3.5 0 1.41-.91 2.72-2 3.25V8.59c.58-.45 1-1.27 1-2.09C10 5.22 8.98 4 8 4H4c-.98 0-2 1.22-2 2.5S3 9 4 9zm9-3h-1v1h1c1 0 2 1.22 2 2.5S13.98 12 13 12H9c-.98 0-2-1.22-2-2.5 0-.83.42-1.64 1-2.09V6.25c-1.09.53-2 1.84-2 3.25C6 11.31 7.55 13 9 13h4c1.45 0 3-1.69 3-3.5S14.5 6 13 6z"></path></svg></a>P2P Network</h1>

<h2><a id="user-content-wide-spectrum-anti-dos-improvements" class="anchor" href="#wide-spectrum-anti-dos-improvements" aria-hidden="true"><svg aria-hidden="true" class="octicon octicon-link" height="16" version="1.1" viewBox="0 0 16 16" width="16"><path fill-rule="evenodd" d="M4 9h1v1H4c-1.5 0-3-1.69-3-3.5S2.55 3 4 3h4c1.45 0 3 1.69 3 3.5 0 1.41-.91 2.72-2 3.25V8.59c.58-.45 1-1.27 1-2.09C10 5.22 8.98 4 8 4H4c-.98 0-2 1.22-2 2.5S3 9 4 9zm9-3h-1v1h1c1 0 2 1.22 2 2.5S13.98 12 13 12H9c-.98 0-2-1.22-2-2.5 0-.83.42-1.64 1-2.09V6.25c-1.09.53-2 1.84-2 3.25C6 11.31 7.55 13 9 13h4c1.45 0 3-1.69 3-3.5S14.5 6 13 6z"></path></svg></a>Wide-spectrum Anti-DoS improvements</h2>

<p>Network traffic represents a grey-scale of useful activity. Some helps the
network to synchronize, while some is for surveillance and deliberate wastage.
Bitcoin Unlimited differentiates useful and less useful data between nodes.</p>

<p>Useful data is primarily valid transactions and blocks, invitations for required
data, and handshake messages.  Not useful data are typically from from spam or
spy nodes, From version 1.0.0, Bitcoin Unlimited Nodes track if peers supply
useful data.</p>

<p>1) All traffic is tracked by byte count, per connection, so the "usefulness" of
data can be measured for what each node is sending or requesting from a BU node.
On inbound messages, useful bytes are anything that is NOT: PING, PONG, VERACK,
VERSION or ADDR messages. Outbound are the same but includes transaction INV.
Furthermore the useful bytes are decayed over time, usually a day.</p>

<p>2) if a peer node does nothing useful within two minutes then the BU node chokes
their INV traffic. A lot of spam nodes normally listen to INV traffic, and this
is first of all a security concern as it is undesirable to allow anyone to just
try and collate INV traffic, but also it's a large waste of bandwidth. So,
quickly they are choked off but are still sent block INV's in case they are
blocks-only nodes.</p>

<p>3) If rogue nodes try to reacquire a connection aggressively a BU node gives
them a 4 hour ban.</p>

<p>There are other types of spam nodes that will do this, so using these three
approaches keeps BU connection slots available for useful nodes and also for the
bit-nodes crawlers that want to find the node counts.</p>

<p>Further information:</p>

<ul>
<li><a href="https://github.com/BitcoiniUnlimited/BitcoiniUnlimited/pull/62">GitHub #PR 62</a></li>
</ul>

<h2><a id="user-content-request-manager-extensions" class="anchor" href="#request-manager-extensions" aria-hidden="true"><svg aria-hidden="true" class="octicon octicon-link" height="16" version="1.1" viewBox="0 0 16 16" width="16"><path fill-rule="evenodd" d="M4 9h1v1H4c-1.5 0-3-1.69-3-3.5S2.55 3 4 3h4c1.45 0 3 1.69 3 3.5 0 1.41-.91 2.72-2 3.25V8.59c.58-.45 1-1.27 1-2.09C10 5.22 8.98 4 8 4H4c-.98 0-2 1.22-2 2.5S3 9 4 9zm9-3h-1v1h1c1 0 2 1.22 2 2.5S13.98 12 13 12H9c-.98 0-2-1.22-2-2.5 0-.83.42-1.64 1-2.09V6.25c-1.09.53-2 1.84-2 3.25C6 11.31 7.55 13 9 13h4c1.45 0 3-1.69 3-3.5S14.5 6 13 6z"></path></svg></a>Request Manager extensions</h2>

<p>Bitcoin Unlimited uses a Request Manager that tracks source nodes for blocks and
transactions. If a request is filled slowly, it reissues the same request to a
different source instead of just waiting and eventually timing out. The Request
Manager was introduced to stabilize Xthin block propagation, but is generally
helpful to improve the synchronization of the mempool and to manage resources
efficiently.</p>

<p>Because synchronized mempools are important for Xpedited (unsolicited Xthin) it
is "bad" for them to be non-synchronized when a node loses track of a source.
Also, if an xpedited block comes in, some transactions may be missing so we can
look up sources for them in the request manager! We don't want to request from
the xpedited source if we can avoid it, because that may be across the GFC. It
is always better to get the transactions from a local source.</p>

<p>With this release, the Request Manager's functionality is extended to the
Initial Blockchain Download (IBD). It eliminates slowdowns and hung connections
which occasionally happen when the request for blocks receives a slow response.</p>

<p>Further information:</p>

<ul>
<li><a href="https://github.com/BitcoiniUnlimited/BitcoiniUnlimited/pull/79">GitHub PR #79</a></li>
<li><a href="https://github.com/BitcoiniUnlimited/BitcoiniUnlimited/pull/229">GitHub PR #229</a></li>
</ul>

<h2><a id="user-content-xthin-block-propagation-optimizations" class="anchor" href="#xthin-block-propagation-optimizations" aria-hidden="true"><svg aria-hidden="true" class="octicon octicon-link" height="16" version="1.1" viewBox="0 0 16 16" width="16"><path fill-rule="evenodd" d="M4 9h1v1H4c-1.5 0-3-1.69-3-3.5S2.55 3 4 3h4c1.45 0 3 1.69 3 3.5 0 1.41-.91 2.72-2 3.25V8.59c.58-.45 1-1.27 1-2.09C10 5.22 8.98 4 8 4H4c-.98 0-2 1.22-2 2.5S3 9 4 9zm9-3h-1v1h1c1 0 2 1.22 2 2.5S13.98 12 13 12H9c-.98 0-2-1.22-2-2.5 0-.83.42-1.64 1-2.09V6.25c-1.09.53-2 1.84-2 3.25C6 11.31 7.55 13 9 13h4c1.45 0 3-1.69 3-3.5S14.5 6 13 6z"></path></svg></a>Xthin block propagation optimizations</h2>

<p>Based on many months of live traffic observation, Bitcoin Unlimited implemented
several general optimizations of the block propagation with Xthin. As tests have
shown, this enables some nodes to operate with zero missing transactions in a
24-hour period and thus achieves extremely efficient use of bandwidth resources
and minimal latency in block propagation.</p>

<p>Further information:</p>

<ul>
<li><a href="https://github.com/BitcoiniUnlimited/BitcoiniUnlimited/pull/131">GitHub PR #131</a></li>
<li><a href="https://github.com/BitcoiniUnlimited/BitcoiniUnlimited/pull/173">GitHub PR #173</a></li>
<li><a href="https://github.com/BitcoiniUnlimited/BitcoiniUnlimited/pull/174">GitHub PR #174</a></li>
<li><a href="https://github.com/BitcoiniUnlimited/BitcoiniUnlimited/pull/176">GitHub PR #176</a></li>
<li><a href="https://github.com/BitcoiniUnlimited/BitcoiniUnlimited/pull/191">GitHub PR #191</a></li>
</ul>

<h1><a id="user-content-mempool-management" class="anchor" href="#mempool-management" aria-hidden="true"><svg aria-hidden="true" class="octicon octicon-link" height="16" version="1.1" viewBox="0 0 16 16" width="16"><path fill-rule="evenodd" d="M4 9h1v1H4c-1.5 0-3-1.69-3-3.5S2.55 3 4 3h4c1.45 0 3 1.69 3 3.5 0 1.41-.91 2.72-2 3.25V8.59c.58-.45 1-1.27 1-2.09C10 5.22 8.98 4 8 4H4c-.98 0-2 1.22-2 2.5S3 9 4 9zm9-3h-1v1h1c1 0 2 1.22 2 2.5S13.98 12 13 12H9c-.98 0-2-1.22-2-2.5 0-.83.42-1.64 1-2.09V6.25c-1.09.53-2 1.84-2 3.25C6 11.31 7.55 13 9 13h4c1.45 0 3-1.69 3-3.5S14.5 6 13 6z"></path></svg></a>Mempool Management</h1>

<h2><a id="user-content-orphan-pool-transaction-management-improvements" class="anchor" href="#orphan-pool-transaction-management-improvements" aria-hidden="true"><svg aria-hidden="true" class="octicon octicon-link" height="16" version="1.1" viewBox="0 0 16 16" width="16"><path fill-rule="evenodd" d="M4 9h1v1H4c-1.5 0-3-1.69-3-3.5S2.55 3 4 3h4c1.45 0 3 1.69 3 3.5 0 1.41-.91 2.72-2 3.25V8.59c.58-.45 1-1.27 1-2.09C10 5.22 8.98 4 8 4H4c-.98 0-2 1.22-2 2.5S3 9 4 9zm9-3h-1v1h1c1 0 2 1.22 2 2.5S13.98 12 13 12H9c-.98 0-2-1.22-2-2.5 0-.83.42-1.64 1-2.09V6.25c-1.09.53-2 1.84-2 3.25C6 11.31 7.55 13 9 13h4c1.45 0 3-1.69 3-3.5S14.5 6 13 6z"></path></svg></a>Orphan Pool transaction management improvements</h2>

<p>Not only blocks, but also transactions can be orphaned. A typical reason is when
the transaction's parent is missing in the mempool. In event of a growing
backlog or of long transaction chains the number of orphaned transactions
increases making management of the mempool resource intensive, further, it
widens a vector for DoS attacks.</p>

<p>The orphan pool is an area that affects Xthin's performance, so managing the
pool by evicting old orphans is important for bloom filter size management. The
number of orphans should reduce once larger blocks occur and mempools are
naturally of a smaller size. Occasionally they reach the current limit of 5000
and their source is not always clear: some might be intentionally created by
some miners, or are an artefact of the trickle logic.</p>

<p>This release restricts the size of orphaned transactions and evicts orphans
after 72 hours. This not only improves node operation, but the synchronization
of mempool data improves Xthin efficiency.</p>

<p>Further information:</p>

<ul>
<li><a href="https://github.com/BitcoiniUnlimited/BitcoiniUnlimited/pull/42">GitHub PR #42</a></li>
<li><a href="https://github.com/BitcoiniUnlimited/BitcoiniUnlimited/pull/100">GitHub PR #100</a></li>
</ul>

<h1><a id="user-content-configuration" class="anchor" href="#configuration" aria-hidden="true"><svg aria-hidden="true" class="octicon octicon-link" height="16" version="1.1" viewBox="0 0 16 16" width="16"><path fill-rule="evenodd" d="M4 9h1v1H4c-1.5 0-3-1.69-3-3.5S2.55 3 4 3h4c1.45 0 3 1.69 3 3.5 0 1.41-.91 2.72-2 3.25V8.59c.58-.45 1-1.27 1-2.09C10 5.22 8.98 4 8 4H4c-.98 0-2 1.22-2 2.5S3 9 4 9zm9-3h-1v1h1c1 0 2 1.22 2 2.5S13.98 12 13 12H9c-.98 0-2-1.22-2-2.5 0-.83.42-1.64 1-2.09V6.25c-1.09.53-2 1.84-2 3.25C6 11.31 7.55 13 9 13h4c1.45 0 3-1.69 3-3.5S14.5 6 13 6z"></path></svg></a>Configuration</h1>

<h2><a id="user-content-command-tweaks" class="anchor" href="#command-tweaks" aria-hidden="true"><svg aria-hidden="true" class="octicon octicon-link" height="16" version="1.1" viewBox="0 0 16 16" width="16"><path fill-rule="evenodd" d="M4 9h1v1H4c-1.5 0-3-1.69-3-3.5S2.55 3 4 3h4c1.45 0 3 1.69 3 3.5 0 1.41-.91 2.72-2 3.25V8.59c.58-.45 1-1.27 1-2.09C10 5.22 8.98 4 8 4H4c-.98 0-2 1.22-2 2.5S3 9 4 9zm9-3h-1v1h1c1 0 2 1.22 2 2.5S13.98 12 13 12H9c-.98 0-2-1.22-2-2.5 0-.83.42-1.64 1-2.09V6.25c-1.09.53-2 1.84-2 3.25C6 11.31 7.55 13 9 13h4c1.45 0 3-1.69 3-3.5S14.5 6 13 6z"></path></svg></a>Command Tweaks</h2>

<p>To allow a better organization of parameters in the command line or the
bitcoin.conf, Bitcoin Unlimited introduces a new format using the dot notation.
For example, to manage connections users can now specify "net.maxConnections" or
"net.maxOutboundConnections".</p>

<p>Parameter access is unified -- the same name is used in bitcoin.conf, via
command line arguments, or via bitcoin-cli (et al). Old parameters in the legacy
format are still supported, while new parameters, like the excessive block size,
are only implemented in the new format.
Try <code>bitcoin-cli get help "\*"</code> to see the list of parameters.</p>

<p>Attention: Setting many of these parameters should be done by user with advanced
knowledge.</p>

<h2><a id="user-content-dns-seed" class="anchor" href="#dns-seed" aria-hidden="true"><svg aria-hidden="true" class="octicon octicon-link" height="16" version="1.1" viewBox="0 0 16 16" width="16"><path fill-rule="evenodd" d="M4 9h1v1H4c-1.5 0-3-1.69-3-3.5S2.55 3 4 3h4c1.45 0 3 1.69 3 3.5 0 1.41-.91 2.72-2 3.25V8.59c.58-.45 1-1.27 1-2.09C10 5.22 8.98 4 8 4H4c-.98 0-2 1.22-2 2.5S3 9 4 9zm9-3h-1v1h1c1 0 2 1.22 2 2.5S13.98 12 13 12H9c-.98 0-2-1.22-2-2.5 0-.83.42-1.64 1-2.09V6.25c-1.09.53-2 1.84-2 3.25C6 11.31 7.55 13 9 13h4c1.45 0 3-1.69 3-3.5S14.5 6 13 6z"></path></svg></a>DNS-Seed</h2>

<p>Whenever a new node starts, it needs to find its peers. Usually this is done by
a DNS seed, which crawls the networks and builds a random list of other nodes to
which the new node can connect to. Currently there are DNS seeds from bluematt,
luke-jr, sipa, 21 and others. Bitcoin Unlimited aims to create its own DNS seed
capable of special requirements such as supporting the service bit of XTHIN. By
adopting and adjusting Sipa's code for a DNS seed Bitcoin Unlimited fixed a
minor bug.</p>

<p>The DNS Seed of Bitcoin Unlimited is currently activated on the NOL (No Limit)
Net, the testnet for Bitcoin Unlimited's Emergent Consensus. It is expected to
go live on the mainnet soon.</p>

<p>Further information:</p>

<ul>
<li><a href="https://github.com/sipa/bitcoin-seeder/pull/42">Github sipa/bicoin-seeder PR# 42</a></li>
</ul>

<h1><a id="user-content-maintenance-and-fixes" class="anchor" href="#maintenance-and-fixes" aria-hidden="true"><svg aria-hidden="true" class="octicon octicon-link" height="16" version="1.1" viewBox="0 0 16 16" width="16"><path fill-rule="evenodd" d="M4 9h1v1H4c-1.5 0-3-1.69-3-3.5S2.55 3 4 3h4c1.45 0 3 1.69 3 3.5 0 1.41-.91 2.72-2 3.25V8.59c.58-.45 1-1.27 1-2.09C10 5.22 8.98 4 8 4H4c-.98 0-2 1.22-2 2.5S3 9 4 9zm9-3h-1v1h1c1 0 2 1.22 2 2.5S13.98 12 13 12H9c-.98 0-2-1.22-2-2.5 0-.83.42-1.64 1-2.09V6.25c-1.09.53-2 1.84-2 3.25C6 11.31 7.55 13 9 13h4c1.45 0 3-1.69 3-3.5S14.5 6 13 6z"></path></svg></a>Maintenance and Fixes</h1>

<p>The release contains a variety of bug fixes, improvements for testing abilities
and maintenance that have been implemented since v0.12.1. Major examples are:</p>

<h2><a id="user-content-reorganisation-of-global-variables-to-eliminate-segfault-bugs" class="anchor" href="#reorganisation-of-global-variables-to-eliminate-segfault-bugs" aria-hidden="true"><svg aria-hidden="true" class="octicon octicon-link" height="16" version="1.1" viewBox="0 0 16 16" width="16"><path fill-rule="evenodd" d="M4 9h1v1H4c-1.5 0-3-1.69-3-3.5S2.55 3 4 3h4c1.45 0 3 1.69 3 3.5 0 1.41-.91 2.72-2 3.25V8.59c.58-.45 1-1.27 1-2.09C10 5.22 8.98 4 8 4H4c-.98 0-2 1.22-2 2.5S3 9 4 9zm9-3h-1v1h1c1 0 2 1.22 2 2.5S13.98 12 13 12H9c-.98 0-2-1.22-2-2.5 0-.83.42-1.64 1-2.09V6.25c-1.09.53-2 1.84-2 3.25C6 11.31 7.55 13 9 13h4c1.45 0 3-1.69 3-3.5S14.5 6 13 6z"></path></svg></a>Reorganisation of global variables to eliminate SegFault bugs</h2>

<p>During tests it was discovered that the shutdown of bitcoind can cause problems.
Sometimes the program wants to free memory which was used for global variables,
while the memory is already free. This can result in the so called SegFault, a
common bug of several C written programs. The cause for this bug is a
mis-organisation of the destruction order used during the shutdown. By
reorganizing global variables in a single file Bitcoin Unlimited fixed the
destruction order and eliminates this bug. This is especially helpful for
running bitcoind test suite.</p>

<p>Further information:</p>

<ul>
<li><a href="https://github.com/BitcoiniUnlimited/BitcoiniUnlimited/pull/67">GitHub PR #67</a></li>
</ul>

<h2><a id="user-content-lock-order-checking-in-debug-mode" class="anchor" href="#lock-order-checking-in-debug-mode" aria-hidden="true"><svg aria-hidden="true" class="octicon octicon-link" height="16" version="1.1" viewBox="0 0 16 16" width="16"><path fill-rule="evenodd" d="M4 9h1v1H4c-1.5 0-3-1.69-3-3.5S2.55 3 4 3h4c1.45 0 3 1.69 3 3.5 0 1.41-.91 2.72-2 3.25V8.59c.58-.45 1-1.27 1-2.09C10 5.22 8.98 4 8 4H4c-.98 0-2 1.22-2 2.5S3 9 4 9zm9-3h-1v1h1c1 0 2 1.22 2 2.5S13.98 12 13 12H9c-.98 0-2-1.22-2-2.5 0-.83.42-1.64 1-2.09V6.25c-1.09.53-2 1.84-2 3.25C6 11.31 7.55 13 9 13h4c1.45 0 3-1.69 3-3.5S14.5 6 13 6z"></path></svg></a>Lock order checking in debug mode</h2>

<p>Bitcoin's debug mode checks that locks are taken in a consistent order to
eliminate the possibility of thread deadlocks. However, when locks were
destructed, they were not removed from the lock order database. Subsequently, if
another lock was allocated in the same memory position, the software would see
it as the same lock. This causes a false positive deadlock detection, and the
resulting exception limits the time that debug mode could be run to no more than
a few days.</p>

<h2><a id="user-content-arm-architecture" class="anchor" href="#arm-architecture" aria-hidden="true"><svg aria-hidden="true" class="octicon octicon-link" height="16" version="1.1" viewBox="0 0 16 16" width="16"><path fill-rule="evenodd" d="M4 9h1v1H4c-1.5 0-3-1.69-3-3.5S2.55 3 4 3h4c1.45 0 3 1.69 3 3.5 0 1.41-.91 2.72-2 3.25V8.59c.58-.45 1-1.27 1-2.09C10 5.22 8.98 4 8 4H4c-.98 0-2 1.22-2 2.5S3 9 4 9zm9-3h-1v1h1c1 0 2 1.22 2 2.5S13.98 12 13 12H9c-.98 0-2-1.22-2-2.5 0-.83.42-1.64 1-2.09V6.25c-1.09.53-2 1.84-2 3.25C6 11.31 7.55 13 9 13h4c1.45 0 3-1.69 3-3.5S14.5 6 13 6z"></path></svg></a>ARM architecture</h2>

<p>Version 1.0.0 has gitian support of deterministic builds for the ARM
architecture. This supplements Windows, iOS and Linux x86, which were previously
offered. This was backported from the Bitcoin Core project and adjusted for the
Bitcoin Unlimited environment.</p>

<h1><a id="user-content-imported-commits" class="anchor" href="#imported-commits" aria-hidden="true"><svg aria-hidden="true" class="octicon octicon-link" height="16" version="1.1" viewBox="0 0 16 16" width="16"><path fill-rule="evenodd" d="M4 9h1v1H4c-1.5 0-3-1.69-3-3.5S2.55 3 4 3h4c1.45 0 3 1.69 3 3.5 0 1.41-.91 2.72-2 3.25V8.59c.58-.45 1-1.27 1-2.09C10 5.22 8.98 4 8 4H4c-.98 0-2 1.22-2 2.5S3 9 4 9zm9-3h-1v1h1c1 0 2 1.22 2 2.5S13.98 12 13 12H9c-.98 0-2-1.22-2-2.5 0-.83.42-1.64 1-2.09V6.25c-1.09.53-2 1.84-2 3.25C6 11.31 7.55 13 9 13h4c1.45 0 3-1.69 3-3.5S14.5 6 13 6z"></path></svg></a>Imported Commits</h1>

<h2><a id="user-content-changes-from-the-open-source-bitcoin-core-project" class="anchor" href="#changes-from-the-open-source-bitcoin-core-project" aria-hidden="true"><svg aria-hidden="true" class="octicon octicon-link" height="16" version="1.1" viewBox="0 0 16 16" width="16"><path fill-rule="evenodd" d="M4 9h1v1H4c-1.5 0-3-1.69-3-3.5S2.55 3 4 3h4c1.45 0 3 1.69 3 3.5 0 1.41-.91 2.72-2 3.25V8.59c.58-.45 1-1.27 1-2.09C10 5.22 8.98 4 8 4H4c-.98 0-2 1.22-2 2.5S3 9 4 9zm9-3h-1v1h1c1 0 2 1.22 2 2.5S13.98 12 13 12H9c-.98 0-2-1.22-2-2.5 0-.83.42-1.64 1-2.09V6.25c-1.09.53-2 1.84-2 3.25C6 11.31 7.55 13 9 13h4c1.45 0 3-1.69 3-3.5S14.5 6 13 6z"></path></svg></a>Changes from the open source Bitcoin Core Project</h2>

<p>Software development is a co-operative process, and especially when several
teams work on open source projects, cooperation becomes a major source of
success when all teams profit from the work of other teams. And while Unlimited
and Core might disagree on some concepts and this disagreement sometimes
dominates the public debates, both teams share the goal of an ongoing
improvement of the codebase.</p>

<p>Bitcoin Unlimited has therefore cherry-picked a number of Bitcoin Core 0.13.x
updates and upgrades which align with BU's onchain-scaling and wider goals to
advance Bitcoin. We would like to thank developers Cory Fields, Jonas
Schnelli, Marco Falke, Michael Ford, Patrick Strateman, Pieter Wuille, Russel
Yanofsky and Wladimir J. van der Laan for their contributions.</p>

<h1><a id="user-content-100-change-log" class="anchor" href="#100-change-log" aria-hidden="true"><svg aria-hidden="true" class="octicon octicon-link" height="16" version="1.1" viewBox="0 0 16 16" width="16"><path fill-rule="evenodd" d="M4 9h1v1H4c-1.5 0-3-1.69-3-3.5S2.55 3 4 3h4c1.45 0 3 1.69 3 3.5 0 1.41-.91 2.72-2 3.25V8.59c.58-.45 1-1.27 1-2.09C10 5.22 8.98 4 8 4H4c-.98 0-2 1.22-2 2.5S3 9 4 9zm9-3h-1v1h1c1 0 2 1.22 2 2.5S13.98 12 13 12H9c-.98 0-2-1.22-2-2.5 0-.83.42-1.64 1-2.09V6.25c-1.09.53-2 1.84-2 3.25C6 11.31 7.55 13 9 13h4c1.45 0 3-1.69 3-3.5S14.5 6 13 6z"></path></svg></a>1.0.0 Change log</h1>

<p>A list commits grouped by author follow. This overview separates commit applied
directly to the BU code from the one imported from Core project. Merge commit
are not included in the list.</p>

<h2><a id="user-content-bu-specific-changes" class="anchor" href="#bu-specific-changes" aria-hidden="true"><svg aria-hidden="true" class="octicon octicon-link" height="16" version="1.1" viewBox="0 0 16 16" width="16"><path fill-rule="evenodd" d="M4 9h1v1H4c-1.5 0-3-1.69-3-3.5S2.55 3 4 3h4c1.45 0 3 1.69 3 3.5 0 1.41-.91 2.72-2 3.25V8.59c.58-.45 1-1.27 1-2.09C10 5.22 8.98 4 8 4H4c-.98 0-2 1.22-2 2.5S3 9 4 9zm9-3h-1v1h1c1 0 2 1.22 2 2.5S13.98 12 13 12H9c-.98 0-2-1.22-2-2.5 0-.83.42-1.64 1-2.09V6.25c-1.09.53-2 1.84-2 3.25C6 11.31 7.55 13 9 13h4c1.45 0 3-1.69 3-3.5S14.5 6 13 6z"></path></svg></a>BU specific changes</h2>

<p>Justaphf (13):</p>

<ul>
<li><code>410fa89</code> Fix too few addnodes breaks RPC addnodes</li>
<li><code>a180db6</code> Fix too many addnode blocks incoming connections</li>
<li><code>013cf0b</code> Added missing memory cleanup for addnode semaphore</li>
<li><code>e55229b</code> Added comments</li>
<li><code>1c661f4</code> Fixed scoping issue with LOCK statement</li>
<li><code>e39ed97</code> Fix issue with initialization on startup</li>
<li><code>50bad14</code> Fix compile error when --disable-wallet</li>
<li><code>37214f7</code> Rebrand Bitcoin Core as Bitcoin</li>
<li><code>beb31fe</code> UI: Corrected tooltips not showing on Ulimited Dlg</li>
<li><code>5951a5d</code> Trivial: Move locks in AcceptConnection</li>
<li><code>98e7158</code> Correct use-after-free in nodehandling.py test.</li>
<li><code>fd18503</code> Protect all FindNode returns against use-after-free</li>
<li><code>71a6c4d</code> [Trival] - Display witness service bit for segwit remote nodes.</li>
</ul>

<p>SartoNess (12):</p>

<ul>
<li><code>ddb29db</code> Append copyright headers</li>
<li><code>c64bf69</code> Update splashscreen</li>
<li><code>37fe3e8</code> Update LicenseInfo</li>
<li><code>772ba24</code> Update rpcuser.py</li>
<li><code>a2eda88</code> Replace Core with Unlimited</li>
<li><code>2597fa5</code> Update unlimited.cpp</li>
<li><code>dd90e40</code> Update splashscreen.cpp</li>
<li><code>b0d6493</code> Update clang-format.py</li>
<li><code>1a3c73b</code> Fixation</li>
<li><code>52930eb</code> Update COPYING</li>
</ul>

<p>bitcoinerrorlog (1):</p>

<ul>
<li><code>8ee64d9</code> Edits for grammar</li>
</ul>

<p>deadalnix (4):</p>

<ul>
<li><code>a990e9b</code> Fix LargestBlockSeen</li>
<li><code>452290d</code> Remove references to LONG_LONG_MAX</li>
<li><code>88d3fc2</code> Add sublime text project file to gitignore</li>
</ul>

<p>dgenr8 (2):</p>

<ul>
<li><code>c902cc4</code> Allow precise tracking of validation sigops/bytes</li>
<li><code>7db79f9</code> Don't allow undefined setminingmaxblock parameter</li>
</ul>

<p>digitsu (1):</p>

<ul>
<li><code>dae894f</code> moved the definition to occur before usage</li>
</ul>

<p>freetrader (21):</p>

<ul>
<li><code>4f5b867</code> [qa] Harmonize unit test suite names with filenames</li>
<li><code>6649e8f</code> [qa] comment out replace-by-fee test as long as RBF is commented out in implementation</li>
<li><code>0e6e117</code> [fix] remove duplicate bad-version check</li>
<li><code>c8406bc</code> adapt -maxuploadtarget option to set the daily buffer according to EB</li>
<li><code>a627cf6</code> [doc] adapt the reduce-traffic to reflect the change to scaling maxupload daily buffer with EB</li>
<li><code>b61ac0b</code> increase baseline SYNC_WITH_PING_TIMEOUT to 60s for my slowest test platform (no impacts on faster ones)</li>
<li><code>dc40b01</code> update comment to reflect to SYNC_WITH_PING timeout increase</li>
<li><code>0a84749</code> [qa] add capability to disable/skip tests and have them show up as such in the log</li>
<li><code>cae87d3</code> [qa] re-enable test which was skipped to demonstrate how a test can be skipped</li>
<li><code>71f86e9</code> [qa] fix up check for tests specified in command line</li>
<li><code>36517ed</code> [qa] add test_classes.py to EXTRA_DIST, for builds set up using distdir target</li>
<li><code>bc102d8</code> [qa] remove rpc-tests.sh which was replaced by Python script</li>
<li><code>a7124b0</code> [qa] add test_classes.py to EXTRA_DIST, for builds set up using distdir target</li>
<li><code>28dda27</code> [qa] add -only-extended option to rpc-tests.py to run only extended tests</li>
<li><code>f78f9ac</code> [qa] add --randomseed option to Python test framework</li>
<li><code>e9bba70</code> [qa] print out seed in BitcoinTestFramework instead of requiring tests to do it</li>
<li><code>e2d6b1d</code> [qa] added doctest to test_classes.py to verify that we can get the full command</li>
<li><code>3f55a97</code> [qa] usability enhancements for rpc-tests.py: listing, better option handling, summary of test results</li>
<li><code>99620ff</code> [qa] show DISABLED, SKIPPED in summary list of test results</li>
<li><code>ba8f167</code> [qa] return exit code 1 if any tests failed (since we catch and collect test failures now for reporting)</li>
<li><code>8dfbb5c</code> add netstyle for 'nol' network</li>
</ul>

<p>gandrewstone (47):</p>

<ul>
<li><code>523d465</code> fix .deb file generation</li>
<li><code>fdf065a</code> fix a seg fault when ctrl-c, add some logging, issue blocks to req mgr whenever INVs come in, fix wallet AcceptToMemPool and SyncWithWallets</li>
<li><code>f5bf490</code> fixes to run some regression tests in BU</li>
<li><code>9e2b8ec</code> re-enable normal tests</li>
<li><code>398f51c</code> save the debug executables for crash analysis before stripping for release</li>
<li><code>f0db510</code> validate header of incoming blocks from BLOCK messages or rpc</li>
<li><code>915167f</code> fix use after erase</li>
<li><code>0b6d71f</code> remove default bip109 flag since BUIP005 signaling is now used in block explorers</li>
<li><code>fd206f2</code> add checks and feedback to setexcessiveblock cli command</li>
<li><code>a1ee57b</code> add CTweak class that gives lets you define an object that can be configured in the CLI, command line or conf file in one declaration.</li>
<li><code>629c524</code> fix execution of excessive unit test</li>
<li><code>a78e8e4</code> fix excessive unit test given new constraint</li>
<li><code>f8a1fb7</code> add a parameter to getpeerinfo that selects the node to display by IP address</li>
<li><code>e7c208f</code> isolate global variables into a separate file to control construction/destruction order</li>
<li><code>b7411da</code> move global vars into globals.cpp</li>
<li><code>192bffc</code> reorganise global variables into a single file so that constructor destructor order is correct</li>
<li><code>4465a7f</code> Merge branch 'accurate_sighash_tracking' of <a href="https://github.com/dgenr8/bitcoin">https://github.com/dgenr8/bitcoin</a> into sigop_sighash</li>
<li><code>33392df</code> move a few more variables into globals.cpp</li>
<li><code>d9c9948</code> incremental memory use as block sizes increase by reworking CFileBuffer to resize the file buffer dynamically during reindex.  remove 32MB message limit (shadowing already existing 16x EB limit).  Increase max JSON message buffer size so large RPC calls can be made (like getblocktemplate).  Add configurable proportional soft limit (uses accept depth) to sigops and max tx size</li>
<li><code>150c86e</code> sigop and transaction size limits and tests</li>
<li><code>3f6096e</code> cleanup formatting, etc</li>
<li><code>b95cd18</code> remove duplicate sighash byte count</li>
<li><code>8828196</code> move net and main cleanup out of global destructor time.</li>
<li><code>46c5148</code> The deadlock detector asserts with false positives, which are detectable because the mutexes that are supposedly in inverse order are different.</li>
<li><code>8c9f0cd</code> resolve additional CCriticalSection destruction dependencies.</li>
<li><code>eb7db8b</code> extended testing and sigops proportional limits</li>
<li><code>bce020d</code> add miner-specific doc for the EC params and pushtx</li>
<li><code>12f2f93</code> fix different handling of gt and lt in github md vs standard md in miner.md</li>
<li><code>d8ef429</code> merge and covert auto_ptr to unique_ptr</li>
<li><code>40079e8</code> Fix boost c++11 undefined symbol in boost::filesystem::copy_file, by not using copy_file</li>
<li><code>1c03745</code> rearrange location of structure decl to satisfy clang c++11 compiler (MAC)</li>
<li><code>595ba29</code> additional LONG_LONG_MAX replacements</li>
<li><code>c322c6f</code> formatting cleanup</li>
<li><code>e87397f</code> fix misspelling of subversionOverride</li>
<li><code>568b564</code> add some prints so travis knows that the test is still going</li>
<li><code>11e3654</code> move long running tests to extended</li>
<li><code>1e08573</code> in block generation, account for block header and largest possible coinbase size.  Clean up excessive tests</li>
<li><code>afaa951</code> protect global statistics object with a critical section to stop multithread simultaneous access</li>
<li><code>3a66102</code> rebase to latest core 0.12 branch</li>
<li><code>f247a97</code> resolve zmq_test hang after test is complete</li>
<li><code>306cc2c</code> change default AD higher to make it costly for a minority hash power to exceed it</li>
<li><code>0c067b4</code> bump revision to 1.0.0</li>
<li><code>20c1f94</code> fix issue where a block's coinbase can make it exceed the configured value</li>
<li><code>3831cc2</code> Add unit test for block generation, and fix a unit test issue -- an invalid configuration left by a prior test</li>
<li><code>5e82003</code> Add unit test for zero reserve block generation, and zero reserve block generation with different length coinbase messages.</li>
<li><code>c831c5d</code> shorten runtime of miner_tests because windows test on travis may be taking too long</li>
<li><code>b471620</code> bump the build number</li>
</ul>

<p>marlengit (1):</p>

<ul>
<li><code>847e750</code> Update CalculateMemPoolAncestors: Remove +1 as per BU: Fix use after free bug</li>
</ul>

<p>nomnombtc (1):</p>

<ul>
<li><code>4c443bf</code> Fix some typos in build-unix.md</li>
</ul>

<p>ptschip (62):</p>

<ul>
<li><code>34b4b85</code> Detailed Protocol Documentation for XTHINs</li>
<li><code>0a61f0a</code> Wallet sync was not happening when sending raw txn</li>
<li><code>1260272</code> Re-able check for end of iterator and general cleanup</li>
<li><code>3338966</code> Prevent the removal of the block hash from request manager too early</li>
<li><code>457ad84</code> Missing cs_main lock on mapOrphanTransactions in requestmanager</li>
<li><code>67983d2</code> Creating new Critical Section for Orphan Cache</li>
<li><code>aebb060</code> General Code cleanup for readability</li>
<li><code>a55b073</code> Do not re-request objects when rate limiting is enabled</li>
<li><code>378cf3d</code> gettrafficshaping LONG_MAX should be LONG_LONG_MAX</li>
<li><code>a3c7631</code> Request Manager Reject Messages: simplify code and clarify reject messages</li>
<li><code>a62c3ec</code> Remove duplicate logic for processing Xthins</li>
<li><code>e16f795</code> Clarify log message when xthin response is beaten</li>
<li><code>6368e50</code> Prevent thinblock from being processed when we already have it</li>
<li><code>efa5d92</code> A few more locks added for cs_orphancache</li>
<li><code>c0f7b24</code> Fix IBD hang issue by using request manager</li>
<li><code>9f3e7a9</code> Auto adjust IBD download settings depending on reponse time</li>
<li><code>be6923a</code> Remove block pacer in request manager when requesting blocks</li>
<li><code>3fc9dd3</code> Filter out the nodes with above average ping times during IBD</li>
<li><code>9d3336f</code> take out one sendexpedited() as not necessary</li>
<li><code>efaaa24</code> Add more detailed logging when expedited connect not possible</li>
<li><code>be095ad</code> Explicitly disconnect if too many connection attempts</li>
<li><code>2266868</code> Fix potential deadlock with cs_orphancache</li>
<li><code>30d00cf</code> Clarify explantion for comments</li>
<li><code>39eb542</code> Revert LogPrint statement back to "net" instead of "thin"</li>
<li><code>7d00163</code> Renable re-requests when traffic shaping is enabled</li>
<li><code>ad048ea</code> Fix regression tests.  Headers not getting downloaded.</li>
<li><code>e8dcc82</code> streamline erase orphans by time</li>
<li><code>8eba8da</code> Do not increase the re-request interval when in regtest</li>
<li><code>7f6352c</code> Sync issues in mempool_reorg.py</li>
<li><code>852ad18</code> Timing fixes for excessive.py test script</li>
<li><code>e10f253</code> Fix compiler warnings</li>
<li><code>8dc91de</code> Don't allow orphans larger than the MAX_STANDARD_TX_SIZE</li>
<li><code>c2133c0</code> Fix a few potential locking issues with cs_orphancache</li>
<li><code>f905207</code> Add CCrititicalSection for thinblock stats</li>
<li><code>90cf11c</code> Remove allowing txns with less than 150000000 in priority are always allowed</li>
<li><code>03b8ced</code> Put lock on flag fishchainnearlysyncd</li>
<li><code>992b252</code> Add unit tests for EraseOrphansByTime()</li>
<li><code>2caa41b</code> Get rid of compiler warning for GetBlockTimeout()</li>
<li><code>b7fb50b</code> Update the unit tests for thinblock_tests.cpp</li>
<li><code>1da0ac4</code> Do not queue jump GET_XTHIN in test environments</li>
<li><code>ea4628f</code> Add cs_orphancache lock to expedited block forwarding</li>
<li><code>46c4aae</code> Fix thinblock stats map interators from going out of range</li>
<li><code>c639235</code> Prevent the regression test from using a port that is arleady in use</li>
<li><code>fe569d5</code> Use Atomic for nLargestBlockSeen</li>
<li><code>d43b3e3</code> remove MainCleanup and NetCleanup from getting called twice</li>
<li><code>8a15e33</code> Move all thinblock related function into thinblock.cpp</li>
<li><code>2171406</code> Create singleton for CThinBlockStats</li>
<li><code>283ac8f</code> Take out namespace from thinblock.h</li>
<li><code>264683c</code> Make it possible to create Deterministic bloom filters for thinblocks</li>
<li><code>f425981</code> Subject coinbase spends to -limitfreerelay</li>
<li><code>ef32f3c</code> do not allow HTTP timeouts to happen in excessive.py</li>
<li><code>96b6ffd</code> Fix compiler warnings for leakybucket.h</li>
<li><code>20109b9</code> Fix maxuploadtarget.py so that it works with python3</li>
<li><code>1330785</code> Fix excessive.py python typeError</li>
<li><code>4d5674a</code> Make txn size is &gt; 100KB for excessive.py</li>
<li><code>9944c95</code> Fix txPerf.py for python3</li>
<li><code>f99f956</code> Fix maxblocksinflight for python3</li>
<li><code>0e8b4db</code> Comptool fix for the requestmanager re-request interval</li>
<li><code>06dd6da</code> Comptool.py - replace missing leading zero from blockhash</li>
<li><code>7f53d6f</code> Improve performance of bip68-112-113-p2p</li>
<li><code>7226212</code> Renable bip69-sequence.py</li>
<li><code>983ce29</code> Fix random hang for excessive.py</li>
</ul>

<p>sickpig (46):</p>

<ul>
<li><code>d7120ee</code> Fix a few typos in Xthinblocks documentation file.</li>
<li><code>71aeb70</code> Update xthin to reflect the existance of maxoutconnnections param</li>
<li><code>ca1cc2d</code> FIX: temp band aid to fix issue #84</li>
<li><code>9fabc25</code> Fix deb package build:</li>
<li><code>f301a17</code> Add quick instructions to build and install BU binaries</li>
<li><code>305eadd</code> Reduce bip68-112-113-p2py exec time from 30 to 3 min.</li>
<li><code>fbef257</code> Use the correct repo in git clone command</li>
<li><code>d7dbf35</code> Use BU repo rather than Core.</li>
<li><code>6e2f7ab</code> Use BU repo rather than Core in gitian doc</li>
<li><code>d94713d</code> Remove leftover from a prev merge</li>
<li><code>f310ac6</code> Add instrusctions to build static binaries</li>
<li><code>24c89e0</code> Fix formatting code para in static binaries sect</li>
<li><code>6f5eed8</code> [travis] disable java based comparison test</li>
<li><code>6dc2eb1</code> [travis] Disable Qt build for i686-pc-linux-gnu platform</li>
<li><code>8d0e84b</code> [travis] Use 3 jobs for 'make check'</li>
<li><code>0e50b8c</code> [travis] Comment out Dust threshold tests.</li>
<li><code>8566300</code> Fix 3 compilation warnings.</li>
<li><code>e3402a1</code> Add copyright and licence headers</li>
<li><code>27994a2</code> Remove declaration of ClearThinblockTimer from unlimited.h</li>
<li><code>a60fd46</code> Add Travis status icon to README.md</li>
<li><code>a16161e</code> Fix README.md install instructions</li>
<li><code>d51e075</code> Fix layout and use correct packet name (libqrencode-dev)</li>
<li><code>49907d0</code> Add a default seeder for the NOL network</li>
<li><code>575b325</code> Travis and rpc tests update</li>
<li><code>7bcf8ce</code> Manual cherry-pick of Core 9ca957b</li>
<li><code>c95f76d</code> switch back to python2</li>
<li><code>ec95cb6</code> fix qt out-of-tree build</li>
<li><code>e31f69c</code> fix qt out-of-tree make deploy</li>
<li><code>11dd4e9</code> Revert "Make possible to build leveldb out of tree"</li>
<li><code>5536d99</code> leveldb: integrate leveldb into our build system</li>
<li><code>0ee5522</code> Out of tree build and rpc test update</li>
<li><code>2f08385</code> Remove a rebase leftover</li>
<li><code>2bc6b9d</code> Update depends</li>
<li><code>8562909</code> Set STRIP var to override automake strip program during make install-strip</li>
<li><code>3640ae7</code> Update config.{guess,sub} for Berkeley DB 4.8.30</li>
<li><code>9742f95</code> Add aarch64 build to Travis-ci</li>
<li><code>38348ff</code> Remove a left-over generate from the last series of cherry pick</li>
<li><code>042758b</code> Merge remote-tracking branch 'upstream/0.12.1bu' into fix/pre-190-status</li>
<li><code>240b6a9</code> [travis] Use python3 to execute rpc tests</li>
<li><code>fce7a14</code> Temporary disable zmq test on travis CI session</li>
<li><code>22acaaf</code> Reanable zmq test on travis</li>
<li><code>99c10e9</code> Fix nol seeder fqdn</li>
<li><code>17d2e02</code> Mark nol seeder as able to filter service bits</li>
<li><code>a7935cf</code> Decouple amd64/i386 gitian build from aarch64/armhf</li>
<li><code>0e3f81b</code> Fix static bitcoin-qt build for x86_64-linux-gnu arch.</li>
<li><code>c1861e6</code> Update Travis-ci status icon (release branch)</li>
</ul>

<p>sandakersmann (1):</p>

<ul>
<li><code>40e5d72</code> Changed http:// to https:// on some links</li>
</ul>

<h2><a id="user-content-commits-imported-from-core" class="anchor" href="#commits-imported-from-core" aria-hidden="true"><svg aria-hidden="true" class="octicon octicon-link" height="16" version="1.1" viewBox="0 0 16 16" width="16"><path fill-rule="evenodd" d="M4 9h1v1H4c-1.5 0-3-1.69-3-3.5S2.55 3 4 3h4c1.45 0 3 1.69 3 3.5 0 1.41-.91 2.72-2 3.25V8.59c.58-.45 1-1.27 1-2.09C10 5.22 8.98 4 8 4H4c-.98 0-2 1.22-2 2.5S3 9 4 9zm9-3h-1v1h1c1 0 2 1.22 2 2.5S13.98 12 13 12H9c-.98 0-2-1.22-2-2.5 0-.83.42-1.64 1-2.09V6.25c-1.09.53-2 1.84-2 3.25C6 11.31 7.55 13 9 13h4c1.45 0 3-1.69 3-3.5S14.5 6 13 6z"></path></svg></a>Commits imported from Core.</h2>

<p>gavinandresen (1):</p>

<ul>
<li><code>d9b8928</code> Allow precise tracking of validation sigops / bytes hashed</li>
</ul>

<p>jonasschnelli (6):</p>

<ul>
<li><code>8dee97f</code> [QA] add fundrawtransaction test on a locked wallet with empty keypool</li>
<li><code>d609895</code> [Wallet] Bugfix: FRT: don't terminate when keypool is empty</li>
<li><code>1df20f7</code> Only pass -lQt5PlatformSupport if &gt;=Qt5.6</li>
<li><code>16572d0</code> Use runtime linking of QT libdbus, use custom/temp. SDK URL</li>
<li><code>61930df</code> Fix bitcoin_qt.m4 and fix-xcb-include-order.patch</li>
<li><code>84ea06e</code> Add support for dnsseeds with option to filter by servicebits</li>
</ul>

<p>laanwj (10):</p>

<ul>
<li><code>15502d7</code> Merge #8187: [0.12.2] backport: [qa] Switch to py3</li>
<li><code>ec0afbd</code> Merge #8176: [0.12.x]: Versionbits: GBT support</li>
<li><code>82e29e8</code> torcontrol: Explicitly request RSA1024 private key</li>
<li><code>b856580</code> build: Enable C++11 build, require C++11 compiler</li>
<li><code>1a388c4</code> build: update ax_cxx_compile_stdcxx to serial 4</li>
<li><code>46f316d</code> doc: Add note about new build/test requirements to release notes</li>
<li><code>c1b7421</code> Merge #9211: [0.12 branch] Backports</li>
<li><code>4637bfc</code> qt: Fix out-of-tree GUI builds</li>
<li><code>0291686</code> tests: Make proxy_test work on travis servers without IPv6</li>
<li><code>2007ba0</code> depends: Mention aarch64 as common cross-compile target</li>
</ul>

<p>sipa (1):</p>

<ul>
<li><code>65be87c</code> Don't set extra flags for unfiltered DNS seed results</li>
</ul>

<p>theuni (17):</p>

<ul>
<li><code>3b40197</code> depends: use c++11</li>
<li><code>9862cf6</code> leveldb: integrate leveldb into our buildsystem</li>
<li><code>452a8b3</code> travis: switch to Trusty</li>
<li><code>c5f329a</code> travis: 'make check' in parallel and verbose</li>
<li><code>6c59843</code> travis: use slim generic image, and some fixups</li>
<li><code>5e85494</code> build: out-of-tree fixups</li>
<li><code>9d31229</code> build: a few ugly hacks to get the rpc tests working out-of-tree</li>
<li><code>41b21fb</code> build: more out-of-tree fixups</li>
<li><code>76a5add</code> build: fix out-of-tree 'make deploy' for osx</li>
<li><code>a99e50f</code> travis: use out-of-tree build</li>
<li><code>adc5125</code> depends: allow for CONFIG_SITE to be used rather than stealing prefix</li>
<li><code>a74b17f</code> gitian: use CONFIG_SITE rather than hijacking the prefix (linux only)</li>
<li><code>2b56ea0</code> depends: only build qt on linux for x86_64/x86</li>
<li><code>9df22f5</code> build: add armhf/aarch64 gitian builds (partial cherry pick)</li>
<li><code>36cf7a8</code> Partial cherry pick f25209a</li>
<li><code>6ed8ab8</code> gitian: use a wrapped gcc/g++ to avoid the need for a system change</li>
<li><code>1f5d5ac</code> depends: fix "unexpected operator" error during "make download"</li>
</ul>

<p>MarcoFalke (6):</p>

<ul>
<li><code>ad99a79</code> [rpcwallet] Don't use floating point</li>
<li><code>913bbfe</code> [travis] Exit early when check-doc.py fails</li>
<li><code>29357e3</code> [travis] Print the commit which was evaluated</li>
<li><code>bd77767</code> [travis] echo $TRAVIS_COMMIT_RANGE</li>
<li><code>6592740</code> [gitian] hardcode datetime for depends</li>
<li><code>f871ff0</code> [gitian] set correct PATH for wrappers (partial cherry-pick)</li>
</ul>

<p>ryanofsky (1):</p>

<ul>
<li><code>cca151b</code> Send tip change notification from invalidateblock</li>
</ul>

<p>pstratem (1):</p>

<ul>
<li><code>f30df51</code> Remove vfReachable and modify IsReachable to only use vfLimited.</li>
</ul>

<p>fanquake (4):</p>

<ul>
<li><code>bc34fc8</code> Fix wake from sleep issue with Boost 1.59.0</li>
<li><code>ddf6e05</code> [depends] Latest config.guess &amp; config.sub</li>
<li><code>99afa8d</code> [depends] OpenSSL 1.0.1k - update config_opts</li>
<li><code>22e47de</code> [trivial] Add aarch64 to depends .gitignore</li>
</ul>

<h1><a id="user-content-credits" class="anchor" href="#credits" aria-hidden="true"><svg aria-hidden="true" class="octicon octicon-link" height="16" version="1.1" viewBox="0 0 16 16" width="16"><path fill-rule="evenodd" d="M4 9h1v1H4c-1.5 0-3-1.69-3-3.5S2.55 3 4 3h4c1.45 0 3 1.69 3 3.5 0 1.41-.91 2.72-2 3.25V8.59c.58-.45 1-1.27 1-2.09C10 5.22 8.98 4 8 4H4c-.98 0-2 1.22-2 2.5S3 9 4 9zm9-3h-1v1h1c1 0 2 1.22 2 2.5S13.98 12 13 12H9c-.98 0-2-1.22-2-2.5 0-.83.42-1.64 1-2.09V6.25c-1.09.53-2 1.84-2 3.25C6 11.31 7.55 13 9 13h4c1.45 0 3-1.69 3-3.5S14.5 6 13 6z"></path></svg></a>Credits</h1>

<p>Thanks to all BU deves who directly contributed to this release:</p>

<ul>
<li>BitcoinErrorLog</li>
<li>Amaury (deadalnix ) Sèchet</li>
<li>Tom (dgenr8) Harding</li>
<li>Jerry (digitsu) Chan</li>
<li>ftrader</li>
<li>Justaphf</li>
<li>marlengit</li>
<li>nomnombtc</li>
<li>Peter (ptschip) Tschipper</li>
<li>sandakersmann</li>
<li>SartoNess</li>
<li>Andrea (sickpig) Suisani</li>
<li>Andrew (theZerg) Stone</li>
</ul>
</article>
  </div>

</div>

<button type="button" data-facebox="#jump-to-line" data-facebox-class="linejump" data-hotkey="l" class="d-none">Jump to Line</button>
<div id="jump-to-line" style="display:none">
  <!-- '"` --><!-- </textarea></xmp> --></option></form><form accept-charset="UTF-8" action="" class="js-jump-to-line-form" method="get"><div style="margin:0;padding:0;display:inline"><input name="utf8" type="hidden" value="&#x2713;" /></div>
    <input class="form-control linejump-input js-jump-to-line-field" type="text" placeholder="Jump to line&hellip;" aria-label="Jump to line" autofocus>
    <button type="submit" class="btn">Go</button>
</form></div>

  </div>
  <div class="modal-backdrop js-touch-events"></div>
</div>


    </div>
  </div>

    </div>

        <div class="container site-footer-container">
  <div class="site-footer" role="contentinfo">
    <ul class="site-footer-links float-right">
        <li><a href="https://github.com/contact" data-ga-click="Footer, go to contact, text:contact">Contact GitHub</a></li>
      <li><a href="https://developer.github.com" data-ga-click="Footer, go to api, text:api">API</a></li>
      <li><a href="https://training.github.com" data-ga-click="Footer, go to training, text:training">Training</a></li>
      <li><a href="https://shop.github.com" data-ga-click="Footer, go to shop, text:shop">Shop</a></li>
        <li><a href="https://github.com/blog" data-ga-click="Footer, go to blog, text:blog">Blog</a></li>
        <li><a href="https://github.com/about" data-ga-click="Footer, go to about, text:about">About</a></li>

    </ul>

    <a href="https://github.com" aria-label="Homepage" class="site-footer-mark" title="GitHub">
      <svg aria-hidden="true" class="octicon octicon-mark-github" height="24" version="1.1" viewBox="0 0 16 16" width="24"><path fill-rule="evenodd" d="M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27.68 0 1.36.09 2 .27 1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.013 8.013 0 0 0 16 8c0-4.42-3.58-8-8-8z"/></svg>
</a>
    <ul class="site-footer-links">
      <li>&copy; 2017 <span title="0.23007s from github-fe142-cp1-prd.iad.github.net">GitHub</span>, Inc.</li>
        <li><a href="https://github.com/site/terms" data-ga-click="Footer, go to terms, text:terms">Terms</a></li>
        <li><a href="https://github.com/site/privacy" data-ga-click="Footer, go to privacy, text:privacy">Privacy</a></li>
        <li><a href="https://github.com/security" data-ga-click="Footer, go to security, text:security">Security</a></li>
        <li><a href="https://status.github.com/" data-ga-click="Footer, go to status, text:status">Status</a></li>
        <li><a href="https://help.github.com" data-ga-click="Footer, go to help, text:help">Help</a></li>
    </ul>
  </div>
</div>



    

    <div id="ajax-error-message" class="ajax-error-message flash flash-error">
      <svg aria-hidden="true" class="octicon octicon-alert" height="16" version="1.1" viewBox="0 0 16 16" width="16"><path fill-rule="evenodd" d="M8.865 1.52c-.18-.31-.51-.5-.87-.5s-.69.19-.87.5L.275 13.5c-.18.31-.18.69 0 1 .19.31.52.5.87.5h13.7c.36 0 .69-.19.86-.5.17-.31.18-.69.01-1L8.865 1.52zM8.995 13h-2v-2h2v2zm0-3h-2V6h2v4z"/></svg>
      <button type="button" class="flash-close js-flash-close js-ajax-error-dismiss" aria-label="Dismiss error">
        <svg aria-hidden="true" class="octicon octicon-x" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M7.48 8l3.75 3.75-1.48 1.48L6 9.48l-3.75 3.75-1.48-1.48L4.52 8 .77 4.25l1.48-1.48L6 6.52l3.75-3.75 1.48 1.48z"/></svg>
      </button>
      You can't perform that action at this time.
    </div>


      
      <script crossorigin="anonymous" integrity="sha256-l96JcOm40vbROHPVCcxZsvNfQQZb1VyY69CSP4/m0eU=" src="https://assets-cdn.github.com/assets/frameworks-97de8970e9b8d2f6d13873d509cc59b2f35f41065bd55c98ebd0923f8fe6d1e5.js"></script>
      <script async="async" crossorigin="anonymous" integrity="sha256-E6d3qJZCMNQSt9GwF9Ew2I3z0ryAIwSfaJYG7ghxzVE=" src="https://assets-cdn.github.com/assets/github-13a777a8964230d412b7d1b017d130d88df3d2bc8023049f689606ee0871cd51.js"></script>
      
      
      
      
    <div class="js-stale-session-flash stale-session-flash flash flash-warn flash-banner d-none">
      <svg aria-hidden="true" class="octicon octicon-alert" height="16" version="1.1" viewBox="0 0 16 16" width="16"><path fill-rule="evenodd" d="M8.865 1.52c-.18-.31-.51-.5-.87-.5s-.69.19-.87.5L.275 13.5c-.18.31-.18.69 0 1 .19.31.52.5.87.5h13.7c.36 0 .69-.19.86-.5.17-.31.18-.69.01-1L8.865 1.52zM8.995 13h-2v-2h2v2zm0-3h-2V6h2v4z"/></svg>
      <span class="signed-in-tab-flash">You signed in with another tab or window. <a href="">Reload</a> to refresh your session.</span>
      <span class="signed-out-tab-flash">You signed out in another tab or window. <a href="">Reload</a> to refresh your session.</span>
    </div>
    <div class="facebox" id="facebox" style="display:none;">
  <div class="facebox-popup">
    <div class="facebox-content" role="dialog" aria-labelledby="facebox-header" aria-describedby="facebox-description">
    </div>
    <button type="button" class="facebox-close js-facebox-close" aria-label="Close modal">
      <svg aria-hidden="true" class="octicon octicon-x" height="16" version="1.1" viewBox="0 0 12 16" width="12"><path fill-rule="evenodd" d="M7.48 8l3.75 3.75-1.48 1.48L6 9.48l-3.75 3.75-1.48-1.48L4.52 8 .77 4.25l1.48-1.48L6 6.52l3.75-3.75 1.48 1.48z"/></svg>
    </button>
  </div>
</div>

  </body>
</html>

