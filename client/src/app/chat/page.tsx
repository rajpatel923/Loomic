import ChatPlaceholder from "@/components/ChatPlaceholder";

export default function ChatPage() {
  return (
    <main className="flex h-dvh min-h-0 flex-1 overflow-hidden px-0 py-0 sm:p-4">
      <div className="h-full w-full min-h-0">
        <ChatPlaceholder />
      </div>
    </main>
  );
}
