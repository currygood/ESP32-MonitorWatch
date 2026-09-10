const path = require('path');
const fs = require('fs');
const { chromium } = require('C:/Users/pole/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright-core');

(async () => {
  const browser = await chromium.launch({
    headless: true,
    executablePath: 'C:/Program Files/Google/Chrome/Application/chrome.exe',
    args: ['--no-sandbox']
  });
  const jobs = [
    { svg: 'docs/diagrams/signal_processing_pipeline.svg', out: 'signal_processing_pipeline.png' },
    { svg: 'docs/diagrams/seizure_model_pipeline.svg', out: 'seizure_model_pipeline.png' },
  ];
  for (const j of jobs) {
    const svg = fs.readFileSync(j.svg, 'utf-8');
    const m = svg.match(/width="(\d+)" height="(\d+)"/);
    const W = parseInt(m[1], 10), H = parseInt(m[2], 10);
    const scale = 2;
    const html = '<!doctype html><html><head><meta charset="utf-8">' +
      '<style>html,body{margin:0;padding:0;background:#fff}svg{display:block;width:100%;height:100%}</style>' +
      '</head><body>' + svg + '</body></html>';
    const htmlPath = path.resolve('docs/diagrams/_preview.html');
    fs.writeFileSync(htmlPath, html, 'utf-8');
    const page = await browser.newPage({ viewport: { width: W * scale, height: H * scale } });
    await page.goto('file:///' + htmlPath.replace(/\\/g, '/'));
    await page.screenshot({ path: j.out });
    await page.close();
    console.log('rendered', j.out, (W * scale) + 'x' + (H * scale));
  }
  await browser.close();
})().catch(e => { console.error('ERR', e.message.split('\n')[0]); process.exit(1); });
