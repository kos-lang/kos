const glob = require('glob');
const path = require('path');
const fs   = require('fs');

module.exports = ({ github, context, artifacts_dir, release_id }) => {
    glob(artifacts_dir + '/**/*', { nodir: true }, async function(err, files) {
        for (const file of files) {
            const filename = path.basename(file);
            console.log(`Uploading ${filename}`);

            await github.repos.uploadReleaseAsset({
                owner: context.repo.owner,
                repo: context.repo.repo,
                release_id: release_id,
                name: filename,
                data: fs.readFileSync(file)
            })
        }
    });
};
