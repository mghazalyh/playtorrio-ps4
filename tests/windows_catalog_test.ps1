$ErrorActionPreference='Stop'
$env:APPDATA=Join-Path $env:RUNNER_TEMP 'ptv-emulated-roaming'
$dir=Join-Path $env:APPDATA 'shadPS4\data\playtorrio'
New-Item -ItemType Directory -Force -Path $dir | Out-Null
[System.IO.File]::WriteAllText((Join-Path $dir 'settings.json'), '{"tmdbApiKey":"fake-test-token","githubSyncEnabled":false}',[System.Text.UTF8Encoding]::new($false))
function Invoke-RestMethod {
  param([string]$Method,[string]$Uri,$Headers,[int]$TimeoutSec,[string]$ErrorAction)
  if($Uri -match '/external_ids'){
    return [pscustomobject]@{imdb_id='tt1234567'}
  }
  if($Uri -match '/tv/'){
    return [pscustomobject]@{results=@(
      [pscustomobject]@{id=200;name='Example TV';first_air_date='2026-10-27';vote_average=8.8;overview='Actual public TMDB metadata shape'}
      [pscustomobject]@{id=201;name='Second Show';first_air_date='2024-01-01';vote_average=7.1;overview='Another show'}
    )}
  }
  if($Uri -match '/movie/'){
    return [pscustomobject]@{results=@(
      [pscustomobject]@{id=300;title='Example Movie';release_date='2026-01-01';vote_average=7.3;overview='A film'}
      [pscustomobject]@{id=301;title='Another Movie';release_date='2025-09-01';vote_average=6.9;overview='Second film'}
    )}
  }
  throw 'Unknown mocked TMDB endpoint'
}
. "$PSScriptRoot\..\HostTools\Refresh-Catalog.ps1"
$catalogPath=Join-Path $dir 'catalog.json'
if(-not (Test-Path $catalogPath)){throw 'Host loader did not write catalog.json'}
$r=Get-Content $catalogPath -Raw | ConvertFrom-Json
if($r.version -ne 1 -or $r.tv.Count -lt 2 -or $r.movies.Count -lt 2){
  throw 'Host loader catalog does not contain the expected TV/movie entries'
}
if($r.tv[0].title -ne 'Example TV' -or $r.tv[0].imdbId -ne 'tt1234567'){
  throw 'Title parsing and IMDb lookup failed'
}
if((Get-Content $catalogPath -Raw).Contains('fake-test-token')){
  throw 'TMDB token was leaked into public catalog.json'
}
Write-Host "PASS: Windows host loader wrote $($r.tv.Count) TV shows and $($r.movies.Count) movies (mocked API)"
