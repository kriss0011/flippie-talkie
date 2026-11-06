// Only this browser's storage is used. No network or cloud upload.
export class RecordingStore {
  constructor(indexedDB = globalThis.indexedDB) {
    this.indexedDB = indexedDB;
    this.pending = null;
  }
  open() {
    if (!this.pending)
      this.pending = new Promise((resolve, reject) => {
        if (!this.indexedDB) {
          reject(new Error('Browser storage is unavailable'));
          return;
        }
        const request = this.indexedDB.open('flippie-talkie-recordings', 1);
        request.onupgradeneeded = () => {
          request.result.createObjectStore('clips', {keyPath: 'id'});
          request.result.createObjectStore('audio');
        };
        request.onsuccess = () => {
          const db = request.result;
          db.onversionchange = () => {
            db.close();
            this.pending = null;
          };
          resolve(db);
        };
        request.onerror = () => reject(request.error);
        request.onblocked = () =>
          reject(new Error('Close other Flippie Talkie tabs and try again'));
      }).catch(error => {
        this.pending = null;
        throw error;
      });
    return this.pending;
  }
  async transaction(mode, action) {
    const db = await this.open();
    return new Promise((resolve, reject) => {
      const tx = db.transaction(['clips', 'audio'], mode);
      let result;
      tx.oncomplete = () => resolve(result?.result);
      tx.onabort = tx.onerror = () =>
        reject(tx.error || new Error('Recording could not be stored'));
      try {
        result = action(tx.objectStore('clips'), tx.objectStore('audio'));
      } catch (error) {
        tx.abort();
        reject(error);
      }
    });
  }
  list() {
    return this.transaction('readonly', clips => clips.getAll());
  }
  audio(id) {
    return this.transaction('readonly', (_, audio) => audio.get(id));
  }
  save(clip) {
    const {blob, ...metadata} = clip;
    return this.transaction('readwrite', (clips, audio) => {
      clips.put(metadata);
      return audio.put(blob, clip.id);
    });
  }
  remove(id) {
    return this.transaction('readwrite', (clips, audio) => {
      clips.delete(id);
      return audio.delete(id);
    });
  }
}
