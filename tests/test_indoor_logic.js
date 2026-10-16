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
    remove(index) { rows.splice(index, 1); },
    setProperty(index, key, value) { rows[index][key] = value; }
};
const messages = [], sent = [];
const stored = {};
const context = vm.createContext({
    localWaypoints, localRevision: 0, localSelected: -1, indoorMode: true,
    reviewedSession: 'session-1', uploadedLocalJson: '', uploadedMissionId: '', uploadedMissionRevision: 0, uploadedSession: '',
    pendingUploadJson: '', pendingUploadRequest: '', pendingUploadSession: '',
    pendingUpdateJson: '', pendingUpdateRequest: '', pendingUpdateSession: '', pendingUpdateMissionId: '', pendingUpdateRevision: 0,
    routeDialog: {close() {}},
    localStore: {setValue(key, value) { stored[key] = value; }, sync() {}},
    actionMessage(message) { messages.push(message); },
    groundLink: { remoteAvailable: true, busy: false,
        mission: {map_session_id: 'session-1', mission_id: 'demo', revision: 1, state: 'READY', completed: 0, total: 1, flight_ready: false},
        uploadLocalRoute(points, session) { return send({operation: 'upload', points, session}); },
        updateLocalRoute(points, replaceFrom, id, revision, session) { return send({operation: 'update', points, replaceFrom, id, revision, session}); },
        startUploadedMission(id, revision, session) { return send({operation: 'start', id, revision, session}); }
    }, console
});
context.page = context;
const task = fs.readFileSync(path.join(__dirname, '../qml/pages/TaskPage.qml'), 'utf8');
for (const name of ['validLocal', 'localJson', 'addLocal', 'moveLocal', 'importLocal', 'uploadLocal', 'startLocal',
    'canEditLocal', 'removeLocal', 'firstLocalChange', 'discardLocalChanges', 'localDraftStatus', 'updateLocal',
    'localPointState', 'clearConfirmedRoute', 'handleMissionChanged', 'finishLocalCommand', 'storeLocalRoute'])
    vm.runInContext(extract(task, name), context);
// Evaluate the actual readonly QML bindings, including dependencies on live progress.
for (const name of ['missionActive', 'landingLocked', 'confirmedRoute', 'dynamicEditable', 'localEditable', 'localReplaceable',
    'lockedPrefixCount', 'routeMatches', 'canUpdateLocal', 'canStartLocal']) {
    const match = new RegExp('readonly property (?:bool|int) ' + name + ': ([\\s\\S]*?)(?=\\n    (?:readonly property|Settings))').exec(task);
    assert(match, 'Missing binding ' + name);
    Object.defineProperty(context, name, {get() { return vm.runInContext('(' + match[1] + '\n)', context); }});
}
function send(command) {
    command.request = 'request-' + (sent.length + 1); sent.push(command);
    context.groundLink.busy = true; context.handleMissionChanged();
    return command.request;
}
function ack(accepted, status = {}) {
    const command = sent[sent.length - 1];
    Object.assign(context.groundLink.mission, status);
    context.groundLink.busy = false;
    context.handleMissionChanged(); // GroundLink emits changed BEFORE commandFinished.
    context.finishLocalCommand(command.request, command.operation, accepted);
}
context.addLocal(1.25, -2.5);
assert.equal(rows.length, 1);
assert.deepStrictEqual(JSON.parse(context.localJson()), [{id: 1, x: 1.25, y: -2.5, z: 1, hover_sec: 2}]);
context.moveLocal(0, 2, -1);
assert.equal(rows[0].z, 1);
assert.equal(rows[0].x, 2);
context.groundLink.busy = true;
context.addLocal(3, 3); context.moveLocal(0, 8, 8);
assert.equal(rows.length, 1); assert.equal(rows[0].x, 2);
context.groundLink.busy = false;
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
context.finishLocalCommand('unrelated', 'upload', true);
assert(!context.confirmedRoute);
ack(true);
assert(context.routeMatches);
assert.equal(stored.routeJson, context.uploadedLocalJson);
context.startLocal(); assert.equal(sent.length, 1);
context.groundLink.mission.flight_ready = true;
context.startLocal(); assert.equal(sent.length, 2);
assert.equal(sent[1].session, 'session-1');
ack(true, {state: 'FLYING', current_waypoint_id: 1});

// Use three confirmed points to exercise future-only editing and versioned updates.
context.groundLink.mission.state = 'READY';
context.addLocal(2, 2); context.addLocal(3, 3);
context.uploadLocal(); ack(true, {total: 3});
context.groundLink.mission.state = 'FLYING';
assert.equal(context.lockedPrefixCount, 1);
const original = context.localJson();
context.moveLocal(0, 9, 9); context.removeLocal(0);
context.importLocal('[]'); context.uploadLocal();
assert.equal(context.localJson(), original);
assert.equal(context.localPointState(0), '执行中');
assert(!context.canUpdateLocal);
context.moveLocal(1, 4, 5);
assert.equal(context.localPointState(1), '待更新');
assert.equal(context.localPointState(0), '执行中'); // Draft edits do not hide actual execution.
context.reviewedSession = ''; context.updateLocal();
assert.equal(sent.length, 3);
context.reviewedSession = 'session-1'; context.updateLocal();
let update = sent[sent.length - 1];
assert.equal(update.operation, 'update'); assert.equal(update.replaceFrom, 1);
assert.equal(update.revision, 2); assert.equal(update.id, 'demo');
assert.deepStrictEqual(JSON.parse(JSON.stringify(update.points.map(p => p.id))), [2, 3]);
const waiting = context.localJson(); context.moveLocal(2, 9, 9); context.addLocal(6, 6);
assert.equal(context.localJson(), waiting); // No mutations while awaiting ACK.
context.finishLocalCommand('unrelated', 'update', true);
assert(context.pendingUpdateRequest.length);
ack(false);
assert(context.confirmedRoute && !context.routeMatches && context.canUpdateLocal);
assert.equal(stored.routeJson, context.uploadedLocalJson); // Rejected drafts never replace the saved route.
context.updateLocal(); ack(true, {revision: 2});
assert(context.routeMatches); assert.equal(context.uploadedMissionRevision, 2);
assert.equal(stored.routeJson, context.uploadedLocalJson);

// Append and truncate use the first changed index, with an empty suffix for deletion.
context.addLocal(4, 4); context.updateLocal();
update = sent[sent.length - 1];
assert.equal(update.replaceFrom, 3); assert.equal(update.points.length, 1);
ack(true, {revision: 3, total: 4});
context.removeLocal(3); context.updateLocal();
update = sent[sent.length - 1];
assert.equal(update.replaceFrom, 3); assert.equal(update.points.length, 0);
ack(true, {revision: 4, total: 3});

// Progress can overtake an unsent edit. Never send a replacement of the new current point.
context.moveLocal(1, 8, 8);
Object.assign(context.groundLink.mission, {completed: 1, current_waypoint_id: 2, state: 'HOVERING'});
assert.equal(context.lockedPrefixCount, 2); assert(!context.canUpdateLocal);
assert.equal(context.localPointState(0), '已完成'); assert.equal(context.localPointState(1), '草稿冲突');
assert(context.localDraftStatus().includes('撤销草稿'));
const count = sent.length; context.updateLocal(); assert.equal(sent.length, count);
context.discardLocalChanges(); assert(context.routeMatches);
assert.equal(context.localPointState(1), '到点悬停');
context.moveLocal(2, 7, 7); context.updateLocal();
assert.equal(sent[sent.length - 1].replaceFrom, 2);
ack(false, {completed: 2, current_waypoint_id: 3, state: 'FLYING'});
assert(context.confirmedRoute); assert(!context.canUpdateLocal);
context.discardLocalChanges();

// Reconnecting to the same confirmed route requires a fresh coordinate review.
context.groundLink.remoteAvailable = false; context.handleMissionChanged();
assert.equal(context.reviewedSession, ''); assert(!context.confirmedRoute);
context.groundLink.remoteAvailable = true; context.handleMissionChanged();
assert(context.confirmedRoute); assert(!context.localEditable);
context.reviewedSession = 'session-1'; assert(context.localEditable);

// Telemetry with the requested version is not an ACK. Unknown results remain unconfirmed.
context.addLocal(9, 9); context.updateLocal();
context.groundLink.mission.revision = 5; context.handleMissionChanged();
assert(context.uploadedLocalJson.length); assert(!context.routeMatches);
ack(false); // Timeout/disconnect after the server may have applied the update.
assert(!context.confirmedRoute); assert(!context.canUpdateLocal);
assert(context.localDraftStatus().includes('尚未确认'));
context.groundLink.mission.map_session_id = 'session-2'; context.handleMissionChanged();
assert.equal(context.reviewedSession, ''); assert.equal(context.uploadedMissionId, '');

// Landing retains the remote lock, while allowing an independent local draft.
context.groundLink.mission.state = 'LANDING';
assert(context.landingLocked && context.localEditable && context.localReplaceable);
assert.equal(context.lockedPrefixCount, 0);
assert(context.localDraftStatus().includes('本地航线'));
const landingCount = rows.length, landingSent = sent.length;
context.removeLocal(0); assert.equal(rows.length, landingCount - 1);
context.importLocal('[]'); assert.equal(rows.length, 0);
context.addLocal(1, 2); assert.equal(rows.length, 1);
assert.equal(context.localPointState(0), '本地草稿');
context.uploadLocal(); context.updateLocal(); context.startLocal();
assert.equal(sent.length, landingSent); // Local deletion must never transmit a command.
assert(!context.canUpdateLocal && !context.canStartLocal);
context.groundLink.busy = true;
context.removeLocal(0); context.importLocal('[]'); assert.equal(rows.length, 1);
assert(!context.localEditable && !context.localReplaceable);
context.groundLink.busy = false;
context.uploadedLocalJson = context.localJson();
context.uploadedMissionId = context.groundLink.mission.mission_id;
context.uploadedMissionRevision = context.groundLink.mission.revision;
context.uploadedSession = context.groundLink.mission.map_session_id;
context.removeLocal(0); assert(!context.routeMatches);
context.discardLocalChanges(); assert.equal(rows.length, 1); assert(context.routeMatches);
Object.assign(context.groundLink.mission, {state: 'FLYING', completed: 0});
assert(!context.localEditable && !context.localReplaceable); // Coordinate review still gates live edits.
assert.equal(context.lockedPrefixCount, 1);

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
console.log('Indoor QML JavaScript checks passed: landing draft edits without transmission, future edits/append/truncate, ACK and revision gates, progress races, reconnect/unknown outcomes, import/start gates and map coordinates.');
