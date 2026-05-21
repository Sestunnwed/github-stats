# github-stats

A high-performance C implementation for generating GitHub profile SVG cards from live GitHub API data.

It produces two badges:
- `generated/overview.svg` — stars, forks, contributions, lines changed, views, repositories
- `generated/languages.svg` — language usage distribution with GitHub language colors

<p align="center">
  <img src="https://github.com/Bingzgoj/github-stats/blob/output/generated/overview.svg" alt="">
  <img src="https://github.com/Bingzgoj/github-stats/blob/output/generated/languages.svg" alt="">
</p>


## Features

- Built in C (`libcurl` + `cJSON`) for fast execution and low overhead
- Uses SVG templates under `templates/`
- Supports repository/language exclusion rules
- Aggregates:
  - repository stars and forks
  - all-time contribution count (GraphQL)
  - lines added/deleted across authored commits
  - repository traffic views (last 14 days)
  - language size/proportion with real language colors
- Structured runtime logs (`INFO`, `WARN`, `ERROR`) for observability

## Requirements

- GCC (C11)
- `libcurl`
- `cJSON`
- GitHub Personal Access Token with sufficient permissions

On Debian/Ubuntu:

```bash
sudo apt-get update
sudo apt-get install -y build-essential libcurl4-openssl-dev libcjson-dev
```

## Build

```bash
make clean && make
```

Binary output: `./github-stats`

## Configuration

Environment variables:

| Variable | Required | Description |
|---|---:|---|
| `ACCESS_TOKEN` | Yes* | GitHub token (preferred) |
| `GITHUB_TOKEN` | Yes* | Fallback token when `ACCESS_TOKEN` is not set |
| `GITHUB_ACTOR` | Yes* | GitHub username/login (preferred) |
| `GITHUB_USER` | Yes* | Fallback user when `GITHUB_ACTOR` is not set |
| `COUNT_STATS_FROM_FORKS` | No | Any non-empty value enables counting forked repos |
| `EXCLUDED` | No | Comma-separated repo patterns to exclude (supports glob), e.g. `owner/private-*,owner/archive/*` |
| `EXCLUDED_LANGS` | No | Comma-separated languages to exclude, e.g. `HTML,CSS,JSON` |

\* At least one token variable and one user variable must be provided.

## Run

```bash
export ACCESS_TOKEN="<your_token>"
export GITHUB_ACTOR="<your_username>"
# optional
export EXCLUDED="owner/private-*,owner/archive/*"
export EXCLUDED_LANGS="HTML,CSS,JSON,YAML,SCSS"

./github-stats
```

Expected stdout:

```text
generated/overview.svg
generated/languages.svg
```

Generated files are written into `generated/`.

## Local cache mode

- `./github-stats`:
  - fetches fresh data from GitHub
  - generates SVG files
  - saves a local cache to `generated/stats_cache.bin`
- `./github-stats -g`:
  - skips GitHub API calls
  - regenerates SVG files from `generated/stats_cache.bin`

## Project Structure

```text
.
├── include/            # headers
├── src/                # C source files
├── templates/          # SVG templates
├── generated/          # output SVG files
└── Makefile
```

## Notes

- GitHub `traffic/views` requires repository access scope; unavailable repos may return partial view stats.
- Very large accounts may take longer due to commit-detail traversal for line-change aggregation.
- If rate limiting occurs, rerun later or use a token with higher quota.

## License
[GNU GPL-3.0](./LICENSE)
