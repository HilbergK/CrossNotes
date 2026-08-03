type DocModule = {
  frontmatter: {
    title?: string;
    nav_order?: number;
    parent?: string;
    has_children?: boolean;
  };
  Content: any;
};

export type DocEntry = {
  path: string;
  slug: string;
  url: string;
  title: string;
  navOrder: number;
  parent?: string;
  hasChildren: boolean;
  load: () => Promise<DocModule>;
};

const modules = import.meta.glob<DocModule>("../../../docs/**/*.md");

// Slugs that are too technical/internal for the public docs site. These are
// dropped from both the nav and route generation, so the pages are not built.
const HIDDEN_SLUGS = new Set([
  "hyphenation-trie-format",
  "activity-manager",
  "i18n",
  "translators",
  "webserver-endpoints",
]);

const visibleModules = Object.fromEntries(
  Object.entries(modules).filter(
    ([path]) => !path.includes("/docs/development/") && !HIDDEN_SLUGS.has(slugFromPath(path)),
  ),
);

function titleFromSlug(slug: string) {
  const leaf = slug.split("/").pop() ?? slug;
  return leaf.replace(/-/g, " ").replace(/\b\w/g, (char) => char.toUpperCase());
}

function slugFromPath(path: string) {
  let slug = path.replace("../../../docs/", "").replace(/\.md$/, "");
  if (slug.endsWith("/README")) {
    slug = slug.replace(/\/README$/, "/index");
  }
  if (slug === "index") {
    slug = "docs";
  }
  return slug;
}

function urlFromSlug(slug: string) {
  return `/${slug}.html`;
}

export async function getDocs(): Promise<DocEntry[]> {
  const entries = await Promise.all(
    Object.entries(visibleModules).map(async ([path, load]) => {
      const mod = await load();
      const slug = slugFromPath(path);
      return {
        path,
        slug,
        url: urlFromSlug(slug),
        title: mod.frontmatter.title ?? titleFromSlug(slug),
        navOrder: mod.frontmatter.nav_order ?? 999,
        parent: mod.frontmatter.parent,
        hasChildren: mod.frontmatter.has_children ?? false,
        load,
      };
    }),
  );

  return entries.sort((a, b) => {
    if (a.navOrder !== b.navOrder) {
      return a.navOrder - b.navOrder;
    }
    return a.title.localeCompare(b.title);
  });
}

export async function getDocBySlug(slug: string) {
  const docs = await getDocs();
  return docs.find((doc) => doc.slug === slug);
}
