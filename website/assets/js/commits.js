document.addEventListener("DOMContentLoaded", function () {
  var list = document.getElementById("commit-list");
  if (!list) return;

  var DATA_KEY = "genix-commits-data";
  var ETAG_KEY = "genix-commits-etag";

  function readCache() {
    try {
      var raw = localStorage.getItem(DATA_KEY);
      return raw ? JSON.parse(raw) : null;
    } catch (e) {
      return null;
    }
  }

  function writeCache(data, etag) {
    try {
      localStorage.setItem(DATA_KEY, JSON.stringify(data));
      if (etag) localStorage.setItem(ETAG_KEY, etag);
    } catch (e) {}
  }

  var cached = readCache();
  if (cached) render(cached);

  var headers = {};
  var etag = localStorage.getItem(ETAG_KEY);
  if (etag) headers["If-None-Match"] = etag;

  fetch("https://api.github.com/repos/zubbledew6/genix/commits?per_page=5", { headers: headers })
    .then(function (res) {
      if (res.status === 304) return null;
      if (!res.ok) throw new Error("bad response");
      var newEtag = res.headers.get("ETag");
      return res.json().then(function (commits) {
        writeCache(commits, newEtag);
        return commits;
      });
    })
    .then(function (commits) {
      if (commits) render(commits);
    })
    .catch(function () {
      if (!cached) {
        list.innerHTML = "";
        var error = document.createElement("li");
        error.className = "commit-status";
        error.textContent = "Unable to load commits.";
        list.appendChild(error);
      }
    });

  function render(commits) {
    list.innerHTML = "";

    if (!commits.length) {
      var empty = document.createElement("li");
      empty.className = "commit-status";
      empty.textContent = "No commits found.";
      list.appendChild(empty);
      return;
    }

    commits.forEach(function (commit) {
      var item = document.createElement("li");
      item.className = "commit-item";

      var avatar = document.createElement("img");
      avatar.className = "commit-avatar";
      avatar.src = commit.author ? commit.author.avatar_url : "assets/images/genix.png";
      avatar.alt = "";

      var info = document.createElement("div");
      info.className = "commit-info";

      var link = document.createElement("a");
      link.className = "commit-message";
      link.href = commit.html_url;
      link.target = "_blank";
      link.rel = "noopener";
      link.textContent = commit.commit.message.split("\n")[0];

      var meta = document.createElement("div");
      meta.className = "commit-meta";

      var sha = document.createElement("span");
      sha.className = "commit-sha";
      sha.textContent = commit.sha.substring(0, 7);

      var author = document.createElement("span");
      author.textContent = commit.commit.author.name;

      var date = document.createElement("span");
      var d = new Date(commit.commit.author.date);
      date.textContent = d.toLocaleDateString();

      meta.appendChild(sha);
      meta.appendChild(author);
      meta.appendChild(date);

      info.appendChild(link);
      info.appendChild(meta);

      item.appendChild(avatar);
      item.appendChild(info);
      list.appendChild(item);
    });
  }
});