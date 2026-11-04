function md5(value) {
    const input = new TextEncoder().encode(value);
    const paddedLength = Math.ceil((input.length + 9) / 64) * 64;
    const bytes = new Uint8Array(paddedLength);
    bytes.set(input);
    bytes[input.length] = 0x80;

    const bitLengthLow = (input.length * 8) >>> 0;
    const bitLengthHigh = Math.floor(input.length / 0x20000000) >>> 0;
    for (let i = 0; i < 4; i++) {
        bytes[paddedLength - 8 + i] = (bitLengthLow >>> (i * 8)) & 0xff;
        bytes[paddedLength - 4 + i] = (bitLengthHigh >>> (i * 8)) & 0xff;
    }

    const shifts = [
        7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
        5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
        4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
        6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
    ];
    const constants = Array.from({ length: 64 }, (_, i) =>
        Math.floor(Math.abs(Math.sin(i + 1)) * 0x100000000) >>> 0
    );
    let a0 = 0x67452301;
    let b0 = 0xefcdab89;
    let c0 = 0x98badcfe;
    let d0 = 0x10325476;

    for (let offset = 0; offset < paddedLength; offset += 64) {
        const words = new Uint32Array(16);
        for (let i = 0; i < 16; i++) {
            const pos = offset + i * 4;
            words[i] = bytes[pos] | (bytes[pos + 1] << 8) |
                (bytes[pos + 2] << 16) | (bytes[pos + 3] << 24);
        }

        let a = a0;
        let b = b0;
        let c = c0;
        let d = d0;
        for (let i = 0; i < 64; i++) {
            let f;
            let g;
            if (i < 16) {
                f = (b & c) | (~b & d);
                g = i;
            } else if (i < 32) {
                f = (d & b) | (~d & c);
                g = (5 * i + 1) % 16;
            } else if (i < 48) {
                f = b ^ c ^ d;
                g = (3 * i + 5) % 16;
            } else {
                f = c ^ (b | ~d);
                g = (7 * i) % 16;
            }

            const sum = (a + f + constants[i] + words[g]) | 0;
            const rotated = (sum << shifts[i]) | (sum >>> (32 - shifts[i]));
            const nextB = (b + rotated) | 0;
            a = d;
            d = c;
            c = b;
            b = nextB;
        }

        a0 = (a0 + a) | 0;
        b0 = (b0 + b) | 0;
        c0 = (c0 + c) | 0;
        d0 = (d0 + d) | 0;
    }

    return [a0, b0, c0, d0].map(word =>
        [0, 8, 16, 24].map(shift =>
            ((word >>> shift) & 0xff).toString(16).padStart(2, "0")
        ).join("")
    ).join("");
}

function findDifferentProperties(objA, objB, prefix = '') {
    const differentProps = {};

    for (const key in objA) {
        if (objA.hasOwnProperty(key)) {
            const propPath = prefix ? `${prefix}.${key}` : key;
            if (typeof objA[key] === 'object' && typeof objB[key] === 'object') {
                const nestedDifferences = findDifferentProperties(objA[key], objB[key], propPath);
                if (Object.keys(nestedDifferences).length > 0) {
                    differentProps[key] = nestedDifferences;
                }
            } else if (objA[key] !== objB[key]) {
                // differentProps[key] = {
                //     oldValue: objA[key],
                //     newValue: objB[key],
                // };
                differentProps[key] = objB[key];
            }
        }
    }
    return differentProps;
}

function flattenObject(obj, parentKey = '') {
    const result = {};

    for (const key in obj) {
        if (obj.hasOwnProperty(key)) {
            const newKey = parentKey ? `${parentKey}.${key}` : key;

            if (typeof obj[key] === 'object' && !Array.isArray(obj[key])) {
                const flattened = flattenObject(obj[key], newKey);
                Object.assign(result, flattened);
            } else {
                result[newKey] = obj[key];
            }
        }
    }

    return result;
}

function byteString(value) {
    const GB = 1024 * 1024 * 1024;
    const MB = 1024 * 1024;
    if (value >= GB) {
        return (value / GB).toFixed(2).toString() + "GB"
    }
    else if (value >= MB) {
        return (value / MB).toFixed(2).toString() + "MB"
    }
    else if (value >= 1024) {
        return (value / 1024).toFixed(2).toString() + "KB"
    }
    else {
        return value.toString() + "byte"
    }
}

function durationFormat(seconds) {
    seconds = Math.floor(seconds);
    const hours = Math.floor(seconds / 3600);
    const minutes = Math.floor((seconds % 3600) / 60);
    const secs = seconds % 60;

    const formattedHours = String(hours).padStart(2, '0');
    const formattedMinutes = String(minutes).padStart(2, '0');
    const formattedSeconds = String(secs).padStart(2, '0');
    return `${formattedHours}:${formattedMinutes}:${formattedSeconds}`;
}