// Runs the actual QML JavaScript functions without requiring a GUI runtime.
// Does not replace the separate Qt/QML rendering and ROS integration checks.
const fs = require('fs');
const vm = require('vm');
const assert = require('assert');
const path = require('path');

function extract(source, name) {
    const pattern = new RegExp('function ' + name + '\\([^)]*\\)\\s*\\{');
    const match = pattern.exec(source);
    assert(match, 'Missing QML function: ' + name);
    const opening = match.index + match[0].length - 1;
    let depth = 1, quote = '', escaped = false;
    for (let i = opening + 1; i < source.length; ++i) {
        const c = source[i];
        if (quote) {
            if (escaped) escaped = false;
            else if (c === '\\') escaped = true;
            else if (c === quote) quote = '';
        } else if (c === '"' || c === "'" || c === '`') quote = c;
        else if (c === '{') ++depth;
        else if (c === '}' && --depth === 0) return source.slice(match.index, i + 1);
    }
    throw new Error('Unclosed function ' + name);
}

const rows = [];
const localWaypoints = {
    get count() { return rows.length; },
    get(index) { return rows[index]; },
    append(point) { rows.push(point); },
    clear() { rows.length = 0; },
    setProperty(index, key, value) { rows[index][key] = value; }
};
const messages = [], sent = [];
const context = vm.createContext({
    localWaypoints, localRevision: 0, localSelected: -1, localEditable: true,
    reviewedSession: 'session-1', uploadedLocalJson: '', canStartLocal: false,
    routeDialog: {close() {}},
    actionMessage(message) { messages.push(message); },
    groundLink: { remoteAvailable: true, mission: {map_session_id: 'session-1', mission_id: 'demo', revision: 1},
        uploadLocalRoute(points, session) { sent.push({points, session}); return 'request-1'; },
        startUploadedMission(id, revision, session) { sent.push({id, revision, session}); }
    }, console
});
context.page = context;
const task = fs.readFileSync(path.join(__dirname, '../qml/pages/TaskPage.qml'), 'utf8');
for (const name of ['validLocal', 'localJson', 'addLocal', 'moveLocal', 'importLocal', 'uploadLocal', 'startLocal'])
    vm.runInContext(extract(task, name), context);
context.addLocal(1.25, -2.5);
assert.equal(rows.length, 1);
assert.deepStrictEqual(JSON.parse(context.localJson()), [{id: 1, x: 1.25, y: -2.5, z: 1, hover_sec: 2}]);
context.moveLocal(0, 2, -1);
assert.equal(rows[0].z, 1);
assert.equal(rows[0].x, 2);
context.localEditable = false;
context.addLocal(3, 3); context.moveLocal(0, 8, 8);
assert.equal(rows.length, 1); assert.equal(rows[0].x, 2);
context.localEditable = true;
context.importLocal('[{"lat":30,"lon":120,"alt":120}]');
assert.equal(rows.length, 1); assert.equal(rows[0].x, 2); // Preserve existing route on failed import.
context.importLocal('[{"x":"1","y":0,"z":1,"hover_sec":2}]');
assert.equal(rows[0].x, 2);
context.importLocal('[{"x":0.5,"y":1,"z":1.2,"hover_sec":3}]');
assert.equal(rows[0].z, 1.2);
context.reviewedSession = '';
context.uploadLocal(); assert.equal(sent.length, 0);
context.reviewedSession = 'session-1'; context.uploadLocal();
assert.equal(sent.length, 1); assert.equal(sent[0].points[0].z, 1.2);
context.startLocal(); assert.equal(sent.length, 1);
context.canStartLocal = true; context.startLocal(); assert.equal(sent.length, 2);
assert.equal(sent[1].session, 'session-1');

const map = fs.readFileSync(path.join(__dirname, '../qml/components/IndoorMapPanel.qml'), 'utf8');
const root = {width: 800, height: 450, centerX: 0, centerY: 0, pixelsPerMeter: 30};
const mapContext = vm.createContext({root, Qt: {point(x, y) { return {x, y}; }}, Math});
for (const name of ['pointFor', 'coordinateAt', 'zoomAt']) vm.runInContext(extract(map, name), mapContext);
for (const key of ['width', 'height', 'centerX', 'centerY', 'pixelsPerMeter'])
    Object.defineProperty(mapContext, key, {get() { return root[key]; }, set(value) { root[key] = value; }});
root.coordinateAt = (x, y) => mapContext.coordinateAt(x, y);
let pixel = mapContext.pointFor(2, -1);
assert.deepStrictEqual(JSON.parse(JSON.stringify(pixel)), {x: 460, y: 255});
let coordinate = mapContext.coordinateAt(pixel.x, pixel.y);
assert.equal(coordinate.x, 2); assert.equal(coordinate.y, -1);
const anchor = root.coordinateAt(500, 300);
mapContext.zoomAt(1.3, 500, 300);
const after = root.coordinateAt(500, 300);
assert(Math.abs(anchor.x - after.x) < 1e-10 && Math.abs(anchor.y - after.y) < 1e-10);
console.log('Indoor QML JavaScript checks passed: route edits/import, session/start gates, pixel/world round trip and anchored zoom.');
