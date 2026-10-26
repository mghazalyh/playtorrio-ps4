param([switch]$Watch, [int]$IntervalMinutes=15)
# PlayTorrio PS4 / shadPS4 Windows TMDB catalog loader.
# Windows HTTPS is used because shadPS4 v0.20 may stub PS4 sceHttpStatus.
# The only shared file sent into PS4 is PUBLIC title metadata.
$ErrorActionPreference='Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$Root=Join-Path $env:APPDATA 'shadPS4\data\playtorrio'
$JsonPath=Join-Path $Root 'catalog.json'
$SettingsPath=Join-Path $Root 'settings.json'
function Update-Catalog {
  try {
    if(-not (Test-Path -LiteralPath $SettingsPath)) { throw "Settings file missing" }
    $config=Get-Content -LiteralPath $SettingsPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $key=[string]$config.tmdbApiKey
    if([string]::IsNullOrWhiteSpace($key)){$key=[string]$config.tmdb_api_key}
    if([string]::IsNullOrWhiteSpace($key) -or $key.StartsWith('PASTE_')){
      throw "A TMDB API key is required in settings.json"
    }
    $headers=@{ 'Accept'='application/json' }
    $useBearer=$key.StartsWith('eyJ')
    if($useBearer){$headers['Authorization']="Bearer $key"}
    # Never print the constructed URL: a TMDB v3 API key lives in its query.
    function Tmdb([string]$path) {
      $url='https://api.themoviedb.org/3'+$path
      $url+= $(if($path.Contains('?')){'&'}else{'?'}) + 'language=en-US'
      if(-not $useBearer){$url+='&api_key='+[uri]::EscapeDataString($key)}
      try {
        return Invoke-RestMethod -Method Get -Uri $url -Headers $headers -TimeoutSec 25 -ErrorAction Stop
      } catch {
        throw "TMDB HTTPS request failed. Check TMDB key and Windows internet connection."
      }
    }
    Write-Host 'Fetching actual trending and popular television shows from TMDB...'
    $trendTv=Tmdb '/trending/tv/week'
    $popularTv=Tmdb '/tv/popular'
    Write-Host 'Fetching actual trending and popular movies from TMDB...'
    $trendMovies=Tmdb '/trending/movie/week'
    $popularMovies=Tmdb '/movie/popular'
    function Make-Items($a,$b,[bool]$isTv){
      $results=[System.Collections.ArrayList]::new()
      $ids=@{}
      foreach($item in @($a.results)+@($b.results)) {
        if($null -eq $item -or $null -eq $item.id){continue}
        $id=[int]$item.id
        if($id -le 0 -or $ids.ContainsKey($id)){continue}
        $ids[$id]=$true
        $title=if($isTv){[string]$item.name}else{[string]$item.title}
        if([string]::IsNullOrWhiteSpace($title)){continue}
        $date=if($isTv){[string]$item.first_air_date}else{[string]$item.release_date}
        $year=if($date.Length -ge 4){$date.Substring(0,4)}else{''}
        $rating=if($null -eq $item.vote_average){0}else{[math]::Round([double]$item.vote_average,1)}
        $record=[ordered]@{
          id=$id; title=$title; year=$year; rating=$rating
          overview=[string]$item.overview; imdbId=''
        }
        [void]$results.Add($record)
        if($results.Count -ge 30){break}
      }
      return ,$results
    }
    $tv=Make-Items $trendTv $popularTv $true
    $movies=Make-Items $trendMovies $popularMovies $false
    # Real IMDb identifiers let the PS4 call the installed Stremio manifest
    # for a selected movie / TV show. Limit detail calls for TMDB rate safety.
    foreach($item in @($tv | Select-Object -First 12)) {
      try {
        $ids=Tmdb ("/tv/"+$item.id+"/external_ids")
        if([string]$ids.imdb_id -match '^tt[0-9]+$'){
          $item.imdbId=[string]$ids.imdb_id
        }
      } catch { } # Metadata still appears even if individual IDs are absent.
    }
    foreach($item in @($movies | Select-Object -First 12)) {
      try {
        $ids=Tmdb ("/movie/"+$item.id+"/external_ids")
        if([string]$ids.imdb_id -match '^tt[0-9]+$'){
          $item.imdbId=[string]$ids.imdb_id
        }
      } catch { }
    }
    $result=[ordered]@{
      version=1
      updated=(Get-Date).ToUniversalTime().ToString('yyyy-MM-dd HH:mm UTC')
      tv=@($tv)
      movies=@($movies)
    }
    if($result.tv.Count -eq 0 -or $result.movies.Count -eq 0){throw 'TMDB returned no titles'}
    New-Item -ItemType Directory -Force -Path $Root | Out-Null
    $temp="$JsonPath.tmp"
    $json=ConvertTo-Json -InputObject $result -Depth 7 -Compress
    if($json.Length -gt 240000){throw 'TMDB catalog too big'}
    [System.IO.File]::WriteAllText($temp,$json,[System.Text.UTF8Encoding]::new($false))
    # Atomic swap; old catalog remains available if download fails.
    if(Test-Path -LiteralPath $JsonPath){Remove-Item -LiteralPath $JsonPath -Force}
    Move-Item -LiteralPath $temp -Destination $JsonPath -Force
    Write-Host ("SUCCESS: Loaded {0} real TV shows and {1} real movies" -f $tv.Count,$movies.Count) -ForegroundColor Green
    Write-Host ("Shared catalog: "+$JsonPath)
    Write-Host 'PlayTorrio PS4 will discover these titles when it reloads its local catalog.'
  } catch {
    # No credentials, URLs with query strings, or HTTP response bodies in output.
    Write-Host ("ERROR: "+$_.Exception.Message) -ForegroundColor Red
    return $false
  }
  return $true
}
do {
  $ok=Update-Catalog
  if(-not $Watch){if(-not $ok){exit 1};break}
  $minutes=[math]::Max(5,$IntervalMinutes)
  Write-Host ("Refreshing again in {0} minutes. Leave this window open; Ctrl+C to stop." -f $minutes)
  Start-Sleep -Seconds (60*$minutes)
} while($true)
