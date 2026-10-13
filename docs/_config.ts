import lume from "lume/mod.ts"
import wiki from "wiki/mod.ts"

import date from "lume/plugins/date.ts"
import redirects from "lume/plugins/redirects.ts"
import relativeUrls from "lume/plugins/relative_urls.ts"

import { autoId } from "./plugins/auto_id.ts"
import "./plugins/prism.ts"
import { languages } from "./plugins/languages.ts"

const site = lume()

site
  .ignore("README.md", "agents", "adr")
  .use(relativeUrls())
  .use(autoId({ languages }))
  .use(wiki({ languages }))
  .use(redirects())
  .use(date({
    locales: {},
    formats: { HUMAN_DATE: "yyyy-MM-dd", HUMAN_DATETIME: "yyyy-MM-dd ppp" },
  }))
  .add("prism.css")
  .copy("media.css")
  .copy("tools")
  .copy("redirect.js")

site.remoteFile("prism.css", "npm:prismjs/themes/prism.min.css")

export default site
