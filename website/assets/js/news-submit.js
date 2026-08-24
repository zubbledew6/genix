    const API_BASE = window.NEWS_API_BASE ?? "";
    const TOKEN_KEY = "genix.news.token";

    const gate = document.getElementById("gate");
    const composer = document.getElementById("composer");
    const gateForm = document.getElementById("gate-form");
    const gateError = document.getElementById("gate-error");
    const newsForm = document.getElementById("news-form");
    const status = document.getElementById("form-status");

    function showComposer() {
      gate.hidden = true;
      composer.hidden = false;
    }
    function showGate() {
      composer.hidden = true;
      gate.hidden = false;
      document.getElementById("access-code").value = "";
    }

    function storedToken() {
      try {
        const raw = sessionStorage.getItem(TOKEN_KEY);
        if (!raw) return null;
        const t = JSON.parse(raw);
        if (!t.token || !t.expiresAt) return null;
        if (Date.now() >= t.expiresAt) {
          sessionStorage.removeItem(TOKEN_KEY);
          return null;
        }
        return t.token;
      } catch { return null; }
    }

    if (storedToken()) showComposer();

    gateForm.addEventListener("submit", async (e) => {
      e.preventDefault();
      gateError.textContent = "";
      const code = document.getElementById("access-code").value;
      try {
        const res = await fetch(API_BASE + "/api/auth", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ code })
        });
        if (res.status === 401) {
          gateError.textContent = "Invalid code.";
          return;
        }
        if (!res.ok) throw new Error("HTTP " + res.status);
        const data = await res.json();
        sessionStorage.setItem(TOKEN_KEY, JSON.stringify(data));
        showComposer();
      } catch {
        gateError.textContent = "Could not reach the server.";
      }
    });

    document.getElementById("signout").addEventListener("click", () => {
      sessionStorage.removeItem(TOKEN_KEY);
      showGate();
    });

    newsForm.addEventListener("submit", async (e) => {
      e.preventDefault();
      status.textContent = "";
      const token = storedToken();
      if (!token) { showGate(); return; }

      const payload = {
        title: document.getElementById("n-title").value.trim(),
        author: document.getElementById("n-author").value.trim(),
        category: document.getElementById("n-category").value || null,
        imageUrl: document.getElementById("n-image").value.trim() || null,
        body: document.getElementById("n-body").value.trim()
      };
      if (!payload.title || !payload.body || !payload.author) {
        status.textContent = "Title, author and body are required.";
        return;
      }

      status.textContent = "Publishing…";
      try {
        const res = await fetch(API_BASE + "/api/news", {
          method: "POST",
          headers: {
            "Content-Type": "application/json",
            "Authorization": "Bearer " + token
          },
          body: JSON.stringify(payload)
        });
        if (res.status === 401) {
          sessionStorage.removeItem(TOKEN_KEY);
          showGate();
          return;
        }
        if (!res.ok) throw new Error("HTTP " + res.status);
        newsForm.reset();
        status.textContent = "Published.";
      } catch {
        status.textContent = "Failed to publish. Try again.";
      }
    });
