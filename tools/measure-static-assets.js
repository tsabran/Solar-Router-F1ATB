// Paste in the browser console on the router origin. Does not alter configuration.
globalThis.staticAssetMeasurementPromise = (async () => {
  const wait = ms => new Promise(resolve => setTimeout(resolve, ms));
  const read = async url => {
    const response = await fetch(url, { cache: "no-store", signal: AbortSignal.timeout(15000) });
    if (!response.ok) throw new Error(`${url}: HTTP ${response.status}`);
    return response;
  };
  const diag = async () => {
    const text = await (await read("/ajax_diag")).text();
    const fields = Object.fromEntries(
      [...text.split("\n")[0].matchAll(/(\w+)=(\d+)/g)].map(m => [m[1], Number(m[2])])
    );
    if (!Number.isFinite(fields.minSinceRead) || fields.minSinceRead === 4294967295) {
      throw new Error(`Invalid /ajax_diag: ${text}`);
    }
    return fields;
  };
  const html = await (await read("/")).text();
  const doc = new DOMParser().parseFromString(html, "text/html");
  const urls = [...doc.querySelectorAll('script[src],link[rel="stylesheet"]')]
    .map(node => node.getAttribute("src") || node.getAttribute("href"))
    .filter(url => new URL(url, location.href).origin === location.origin)
    .filter(url => !["/RuntimeJS", "/runtime.css"].includes(new URL(url, location.href).pathname));
  if (urls.length !== 6) throw new Error(`Expected 6 resources, got ${urls.length}`);
  const samples = [];
  for (let pass = 1; pass <= 4; pass++) {
    for (const group of [...urls.map(url => [url]), urls]) {
      await diag();
      await wait(500);
      const before = await diag();
      if (before.free < 8192) throw new Error(`Aborting: only ${before.free} free bytes`);
      const responses = await Promise.all(group.map(async url => {
        const response = await read(url);
        const bytes = (await response.arrayBuffer()).byteLength;
        const contentLength = response.headers.get("Content-Length");
        return { url, decodedBytes: bytes,
          wireBytes: contentLength === null ? null : Number(contentLength),
          cacheControl: response.headers.get("Cache-Control"),
          encoding: response.headers.get("Content-Encoding") };
      }));
      const after = await diag();
      const sample = { pass, resource: group.length === 1 ? group[0] : "parallel-6",
        before: before.minSinceRead, after: after.minSinceRead,
        drop: before.minSinceRead - after.minSinceRead, responses };
      samples.push(sample);
      console.log(sample);
      await wait(1000);
    }
  }
  console.table(samples.map(({ responses, ...sample }) => sample));
  const report = { date: new Date().toISOString(), origin: location.origin, samples };
  globalThis.staticAssetMeasurements = report;
  console.log(JSON.stringify(report, null, 2));
  return report;
})().catch(error => { console.error("Static asset measurement failed", error); throw error; });
