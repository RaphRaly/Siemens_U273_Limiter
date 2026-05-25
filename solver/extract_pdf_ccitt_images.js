"use strict";

const fs = require("node:fs");
const path = require("node:path");

function readScalarObjectLengths(pdfText) {
  const lengths = new Map();
  const re = /(\d+)\s+0\s+obj\s+(\d+)\s+endobj/g;
  for (let m = re.exec(pdfText); m; m = re.exec(pdfText)) {
    lengths.set(Number(m[1]), Number(m[2]));
  }
  return lengths;
}

function findObjectStart(pdfText, beforePos) {
  const re = /(\d+)\s+0\s+obj/g;
  let last = null;
  for (let m = re.exec(pdfText); m && m.index < beforePos; m = re.exec(pdfText)) {
    last = { objectNumber: Number(m[1]), start: m.index };
  }
  return last;
}

function parseNumber(dict, name) {
  const re = new RegExp(`/${name}\\s+([0-9]+)`);
  const m = dict.match(re);
  return m ? Number(m[1]) : null;
}

function parseLength(dict, lengths) {
  const ref = dict.match(/\/Length\s+(\d+)\s+0\s+R/);
  if (ref) {
    return lengths.get(Number(ref[1])) ?? null;
  }
  return parseNumber(dict, "Length");
}

function afterStreamMarker(pdfBuffer, streamPos) {
  let pos = streamPos + "stream".length;
  if (pdfBuffer[pos] === 0x0d && pdfBuffer[pos + 1] === 0x0a) {
    return pos + 2;
  }
  if (pdfBuffer[pos] === 0x0a || pdfBuffer[pos] === 0x0d) {
    return pos + 1;
  }
  return pos;
}

function tiffEntry(buffer, offset, tag, type, count, value) {
  buffer.writeUInt16LE(tag, offset);
  buffer.writeUInt16LE(type, offset + 2);
  buffer.writeUInt32LE(count, offset + 4);
  if (type === 3 && count === 1) {
    buffer.writeUInt16LE(value, offset + 8);
    buffer.writeUInt16LE(0, offset + 10);
  } else {
    buffer.writeUInt32LE(value, offset + 8);
  }
}

function ccittToTiff(ccittData, width, height, photometric) {
  const entries = [
    [256, 4, 1, width],
    [257, 4, 1, height],
    [258, 3, 1, 1],
    [259, 3, 1, 4],
    [262, 3, 1, photometric],
    [266, 3, 1, 1],
    [273, 4, 1, 0],
    [278, 4, 1, height],
    [279, 4, 1, ccittData.length],
    [284, 3, 1, 1],
    [293, 4, 1, 0],
  ].sort((a, b) => a[0] - b[0]);

  const ifdOffset = 8;
  const ifdByteCount = 2 + entries.length * 12 + 4;
  const imageOffset = ifdOffset + ifdByteCount;
  const out = Buffer.alloc(imageOffset + ccittData.length);

  out.write("II", 0, "ascii");
  out.writeUInt16LE(42, 2);
  out.writeUInt32LE(ifdOffset, 4);
  out.writeUInt16LE(entries.length, ifdOffset);

  entries.forEach((entry, index) => {
    const value = entry[0] === 273 ? imageOffset : entry[3];
    tiffEntry(out, ifdOffset + 2 + index * 12, entry[0], entry[1], entry[2], value);
  });
  out.writeUInt32LE(0, ifdOffset + 2 + entries.length * 12);
  ccittData.copy(out, imageOffset);
  return out;
}

function extractImages(pdfPath, outDir) {
  fs.mkdirSync(outDir, { recursive: true });
  const pdfBuffer = fs.readFileSync(pdfPath);
  const pdfText = pdfBuffer.toString("latin1");
  const lengths = readScalarObjectLengths(pdfText);

  const images = [];
  let searchFrom = 0;
  while (true) {
    const imagePos = pdfText.indexOf("/Subtype /Image", searchFrom);
    if (imagePos < 0) {
      break;
    }
    const objectStart = findObjectStart(pdfText, imagePos);
    const objectNumber = objectStart ? objectStart.objectNumber : null;
    const streamPos = pdfText.indexOf("stream", imagePos);
    const dict = pdfText.slice(objectStart ? objectStart.start : imagePos, streamPos);
    const filterMatch = dict.match(/\/Filter\s+\/([A-Za-z0-9]+)/);
    const filter = filterMatch ? filterMatch[1] : "UNKNOWN";
    const width = parseNumber(dict, "Width");
    const height = parseNumber(dict, "Height");
    const length = parseLength(dict, lengths);
    if (!objectNumber || !width || !height || !length) {
      throw new Error(`Cannot parse image near byte ${imagePos}`);
    }

    const dataStart = afterStreamMarker(pdfBuffer, streamPos);
    const data = pdfBuffer.subarray(dataStart, dataStart + length);
    const basename = `pdf_image_obj${objectNumber}_${width}x${height}`;
    if (filter === "DCTDecode") {
      fs.writeFileSync(path.join(outDir, `${basename}.jpg`), data);
      images.push({ objectNumber, filter, width, height, file: `${basename}.jpg` });
    } else if (filter === "CCITTFaxDecode") {
      const whiteIsZero = `${basename}_photometric0.tif`;
      const blackIsZero = `${basename}_photometric1.tif`;
      fs.writeFileSync(path.join(outDir, whiteIsZero), ccittToTiff(data, width, height, 0));
      fs.writeFileSync(path.join(outDir, blackIsZero), ccittToTiff(data, width, height, 1));
      images.push({ objectNumber, filter, width, height, file: whiteIsZero });
      images.push({ objectNumber, filter, width, height, file: blackIsZero });
    } else {
      fs.writeFileSync(path.join(outDir, `${basename}.bin`), data);
      images.push({ objectNumber, filter, width, height, file: `${basename}.bin` });
    }
    searchFrom = dataStart + length;
  }

  fs.writeFileSync(path.join(outDir, "manifest.json"), JSON.stringify({ pdfPath, images }, null, 2));
  return images;
}

if (require.main === module) {
  const pdfPath = process.argv[2] || path.join(__dirname, "..", "Siemens_U273_Limiter.pdf");
  const outDir = process.argv[3] || path.join(__dirname, "..", "results", "pdf_evidence");
  const images = extractImages(pdfPath, outDir);
  console.log(`Extracted ${images.length} image artifacts to ${outDir}`);
  for (const image of images) {
    console.log(`${image.file} ${image.filter} ${image.width}x${image.height}`);
  }
}

module.exports = {
  extractImages,
};
