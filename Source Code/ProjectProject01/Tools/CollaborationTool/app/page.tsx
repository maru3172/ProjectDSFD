import { getChatGPTUser } from "./chatgpt-auth";
import WorkspaceApp from "./workspace-app";
export const dynamic="force-dynamic";
export default async function Page(){return <WorkspaceApp user={await getChatGPTUser()}/>}