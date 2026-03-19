Import("env")

# After every firmware upload, automatically upload the LittleFS filesystem image.
# This keeps data/ files (index.html, config.html, config.json) in sync with the
# firmware regardless of how the upload is triggered (IDE button, CLI, or targets=).
def upload_fs(source, target, env):
    env.Execute("pio run -t uploadfs -e " + env["PIOENV"])

env.AddPostAction("upload", upload_fs)
