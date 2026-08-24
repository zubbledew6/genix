 const NEWS_API = (window.NEWS_API_BASE ?? "") + "/api/news";

 const escapeHtml = (s) => String(s ?? "").replace(/[&<>"']/g, c => ({
   "&": "&amp;", "<": "&lt;", ">": "&gt;", "\"": "&quot;", "'": "&#39;"
 }[c]));

 const paragraphs = (text) => escapeHtml(text)
   .split(/\n{2,}/)
   .map(p => "<p>" + p.replace(/\n/g, "<br>") + "</p>")
   .join("");

 const formatDate = (iso) => {
   const d = new Date(iso);
   if (isNaN(d)) return "";
   return d.toLocaleString(undefined, { dateStyle: "medium", timeStyle: "short" });
 };

 const renderItem = (item) => {
   const metaParts = [];

   if (item.category) {
     metaParts.push(`<span class="news-tag">${escapeHtml(item.category)}</span>`);
   }
   if (item.author) {
     metaParts.push(`<span class="news-author">by ${escapeHtml(item.author)}</span>`);
   }
   if (item.createdAt) {
     metaParts.push(`<time datetime="${escapeHtml(item.createdAt)}">${escapeHtml(formatDate(item.createdAt))}</time>`);
   }

   const metaHtml = metaParts.join(' <span class="meta-separator">•</span> ');
   const img = item.imageUrl
     ? `<img class="news-image" src="${escapeHtml(item.imageUrl)}" alt="">`
     : "";

   return `
     <article class="news-item">
       <header class="news-item-head">
         <h3>${escapeHtml(item.title)}</h3>
         <div class="news-meta">${metaHtml}</div>
       </header>
       ${img}
       <div class="news-body">${paragraphs(item.body)}</div>
     </article>
   `;
 };

 async function loadNews() {
   const status = document.getElementById("news-status");
   const list = document.getElementById("news-list");
   try {
     const res = await fetch(NEWS_API, { cache: "no-store" });
     if (!res.ok) throw new Error("HTTP " + res.status);
     const data = await res.json();
     const items = Array.isArray(data.items) ? data.items : [];
     if (items.length === 0) {
       status.textContent = "No news yet. Check back soon.";
       list.innerHTML = "";
       return;
     }
     status.textContent = "";
     list.innerHTML = items.map(renderItem).join("");
   } catch (err) {
     status.textContent = "News is currently unavailable.";
     list.innerHTML = "";
   }
 }

 loadNews();
