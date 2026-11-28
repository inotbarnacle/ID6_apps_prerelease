# Notes for coding agents

## Publishing

`tools/publish_release.py publish` sends each app's release to Flare Drive's plugin catalog through `flare-publish`
(Flare Drive's `tools/flare_publish.py`, in the repo beside this one). The publishing Worker checks and stores it, and
the per-plugin list for Flare Drive 2.2.1 and earlier is rebuilt from the catalog. `stability` changes the catalog the
same way. Read [Flare Drive's docs/publishing.md](../flare-drive/docs/publishing.md)
(https://github.com/CallMeLewis/flare-drive/blob/main/docs/publishing.md) before publishing or changing a release: it
describes the whole process.

- Never upload an archive or `index.json` to the bucket any other way, for example with Wrangler. Flare Drive 2.2.2 and
  later read only the catalog, so they would never see it.
- `retire` is refused: the catalog would keep listing the release. Withdraw the app or change a release on the
  publishing page (`publish.darkzone.dev`) instead, as that guide describes.
- `plugins/<id>.toml` is each app's manifest. Its `folder` and `worker` can never change, since installs on cars use
  them. Its name, summary, description and order only describe an app new to the catalog; edit them on the publishing
  page. A new app needs a manifest here and its id in `PLUGINS` in `tools/publish_release.py`.
