{
    'targets': [{
        'target_name': 'vivaldi_app',
        'type': 'none',
        'variables': {
          'gulp_dest': '<(PRODUCT_DIR)/resources/vivaldi',
          'static_resources': [ '<!@(python bin/list_dir.py ./src/resources)'],
          'static_default_bookmarks': [ '<!@(python bin/list_dir.py ./src/default-bookmarks)'],
          'userfiles': [ '<!@(python bin/list_dir.py ./src/user_files)'],
          'gulp_sources':  [ '<!@(git ls-files "src/*.js" "src/*.jsx" "src/*.less")' ],
          'js_outputs': [
            '<(gulp_dest)/bundle.js',
            '<(gulp_dest)/vendor-bundle.js',
            '<(gulp_dest)/common-bundle.js',
          ],
          'background_outputs': ['<(gulp_dest)/background-bundle.js'],
          'browserjs_outputs': ['<(gulp_dest)/browser-bundle.js'],
          'conditions': [
            ['buildtype=="Official"', {   # <---- Enable/Disable features not ready for broader testing
              'officialBuild': 1,
              'mailreader_worker_sources': [],
              'mailreader_worker_outputs': [],
            }, {
              'officialBuild': 0,
              'mailreader_worker_outputs': ['<(gulp_dest)/bundle-mailreader-worker.js'],
            }],
          ],
          'less_outputs': [
                '<(gulp_dest)/style/common.css'
          ],
          'translation_sources': [ '<!@(git ls-files "src/*.po")' ],
          'translation_outputs': [ '<!@(python bin/list_tran_output.py)' ],
        },
        'actions': [
          {
            'action_name': 'bundle',
            'inputs': [
              './webpack.config.js',
              './.babelrc',
              './package.json',
              '<@(gulp_sources)',
            ],
            'outputs': [
              '<(gulp_dest)',
              '<@(js_outputs)',
              '<@(background_outputs)',
              '<@(browserjs_outputs)',
              '<@(mailreader_worker_outputs)',
              '<@(less_outputs)',
            ],
            'action': [
              'node',
              'node_modules/webpack/bin/webpack.js',
              '--configuration',
              '<(CONFIGURATION_NAME)',
              '--bail',
              '--dest',
              '<(gulp_dest)',
              '--officialBuild',
              '<(officialBuild)',
            ],
          },
          {
            'action_name': 'translations',
            'inputs': [
              './bin/po2messagesjson.js',
              '<@(translation_sources)',
            ],
            'outputs': [
              '<@(translation_outputs)',
            ],
            'action': [
              'node',
              'bin/po2messagesjson.js',
              '--dest',
              '<(gulp_dest)',
            ],
          },
        ],
        'copies': [
          {
            'destination': '<(PRODUCT_DIR)/resources/vivaldi/resources',
            'files': [
              '<@(static_resources)',
            ],
          },
          {
            'destination': '<(PRODUCT_DIR)/resources/vivaldi/default-bookmarks',
            'files': [
              '<@(static_default_bookmarks)',
            ],
          },
          {
            'destination': '<(PRODUCT_DIR)/resources/vivaldi/user_files',
            'files': [
              '<@(userfiles)',

            ],
          },
          {
            'destination': '<(PRODUCT_DIR)/resources/vivaldi',
            'files': [
              './src/browser.html'
            ],
          },
          {
            'destination': '<(PRODUCT_DIR)/resources/vivaldi/components/startpage',
            'files': [
              './src/components/startpage/startpage.html',
            ],
          },
          {
            'destination': '<(PRODUCT_DIR)/resources/vivaldi/components/settings',
            'files': [
              './src/components/settings/settings.html',
            ],
          },
          {
            'destination': '<(PRODUCT_DIR)/resources/vivaldi/components/welcome',
            'files': [
              './src/components/welcome/welcome.html',
            ],
          },
          {
            'destination': '<(PRODUCT_DIR)/resources/vivaldi/components/mail',
            'files': [
              './src/components/mail/mail.html',
            ],
          },
          {
            'destination': '<(PRODUCT_DIR)/resources/vivaldi/util',
            'files': [
              './src/util/internalPageUtil.js',
            ],
          },
          {
            'destination': '<(PRODUCT_DIR)/resources/vivaldi/components/thumbnail',
            'files': [
              './src/components/thumbnail/capture.html',
            ],
          },
        ],
    }]
}
