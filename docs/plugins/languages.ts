import { SEPARATOR } from "jsr:@std/path@^1.1.1/constants"

export const languages = ["en"]
export const getLangPattern = (languages: string[]) =>
  new RegExp(`^\\${SEPARATOR}(${languages.join("|")})(?=\\${SEPARATOR}|$)`)
